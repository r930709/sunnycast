#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <assert.h>
#include <errno.h>

#include "sx_pkt.h"
#include "sx_desc.h"
#include "sx_thread.h"
#include "sx_thread_priority.h"
#include "sx_types.h"
#include "sx_pipe.h"
#include "sx_queue.h"

/* -------------------------------------------------
/       ePKT_TYPE 
/              enum packet type(audio->0),(video->1),(NULL->2)
/---------------------------------------------------*/
typedef enum{
	PKT_TYPE_AUDIO,
	PKT_TYPE_VIDEO,
	PKT_TYPE_NULL
} ePKT_TYPE;



/* -------------------------------------------------
/       sMGMT_DATA_CBLK 
/              the object of sMGMT_DATA_control_block which owns pkt_process_thread
/---------------------------------------------------*/
typedef struct{
	
	pthread_t	pkt_process_thread;

} sMGMT_DATA_CBLK;

static sMGMT_DATA_CBLK		f_cblk;



/* -------------------------------------------------
/       sx_mgmt_m2ts_decoder_init() 
/              MPEG2_TS Decoder init,do nothing 
/---------------------------------------------------*/
void sx_mgmt_m2ts_decoder_init(void){
	
	printf("(mgmt_m2ts): sx_mgmt_m2ts_decoder_init(): Initialized.\n");
}

/* -------------------------------------------------
/       pcr_get() 
/  		From a group of MPEG2-TS packets,continue to find pcr value which get by shift pointer,until this RTP payload was searched over             
/---------------------------------------------------*/
static UINT64 pcr_get(
	sSX_DESC	*desc
){
	UINT8	*curr_ptr;
	UINT32	bytes_left;
	
	/*Get current pkt data */
	curr_ptr = desc->data;
	
	/* Get packet length */
	bytes_left = desc->data_len;
	assert(bytes_left > sizeof(sRTP_HDR));

	/* GET MPEG2-TS Header */
	curr_ptr += sizeof(sRTP_HDR);	///< point to the RTP payload initial address
	
	/* GET MPEG2-TS header+payload bytes(size of packet left) */
	bytes_left -= sizeof(sRTP_HDR);
	assert((bytes_left % sizeof(sMPEG2_TS)) == 0);	///< MPEG2-TS HDR + payload(184 bytes)

	do{
		sMPEG2_TS	*ts = (sMPEG2_TS *) curr_ptr;
		UINT16 		pid = PID_GET(ts->hdr);
		
		/* The pid of PCR defined by wfd is 0x1000 */
		if(pid == 0x1000){	
			curr_ptr += (sizeof(sMPEG2_TS_HDR) + 2);   ///< shift mpeg2-ts hdr+2(adpation filed length,
								   ///<                      discountinuity indicator,
								   ///<                      random access indicator,
								   ///<			     elementary stream priority indicator,
			                                           ///<)		     5 flags(PCR_flag are one of them)
			UINT64	pcr = 0;
			UINT32	i;
			for(i = 0; i < 6; i++){
				pcr = ((pcr << 8) | curr_ptr[i]);	///< pcr content(pcr_base(33)+reserved(6)+pcr_extension(9) = 48bits)
			}
			UINT64	pcr_base = (pcr >> (9+6));
			UINT64	pcr_ext  = (pcr & (0x1FF));

			pcr = pcr_base * 300 + pcr_ext;		///< pcr_val = (base * 300)+ ext [from wikipedia]
			UINT64	pcr_ms = pcr / 27000;		///< The last 9 are based on a 27MHz clock [from wikipedia]
								///< pcr_ms = (pcr * (10^3))/ 27MHz = pcr / 27000 
			return pcr_ms;
		}	
	
		bytes_left -= sizeof(sMPEG2_TS);	///< jump across (MPEG2-TS HDR+payload(184 bytes)),point to the next MPEG2-TS HDR
		curr_ptr   += sizeof(sMPEG2_TS);
	} while(bytes_left > 0);
		
	return 0;
}

/* -------------------------------------------------
/       pkt_type_get()
/  		From a group of MPEG2-TS packets,continue to distinguish pid value which get by from MPEG2-TS HDR,until this RTP payload was searched over             
/---------------------------------------------------*/
static ePKT_TYPE pkt_type_get(
	sSX_DESC	*desc
){
	UINT8	*curr_ptr;
	UINT32	bytes_left;

	/*TODO repeat shift behavior*/
	/*Get current pkt data */
	curr_ptr = desc->data;
	
	/* Get packet length */
	bytes_left = desc->data_len;
	assert(bytes_left > sizeof(sRTP_HDR));

	/* GET MPEG2-TS Header */
	curr_ptr += sizeof(sRTP_HDR);	///< point to the RTP payload initial address

	/* GET MPEG2-TS header+payload bytes(size of packet left) */
	bytes_left -= sizeof(sRTP_HDR);
	assert((bytes_left % sizeof(sMPEG2_TS)) == 0);	///< MPEG2-TS HDR + payload(184 bytes)
	
	do{
		sMPEG2_TS	*ts = (sMPEG2_TS *) curr_ptr;
		UINT16		pid = PID_GET(ts->hdr);

		if(pid == 0x1011){		///<  The pid of video defined by wfd is 0x1011 
			return PKT_TYPE_VIDEO;
		}
		else if(pid == 0x1100){ 	///< The pid of audio defined by wfd is 0x1100 
			return PKT_TYPE_AUDIO;
		}		
		
		bytes_left -= sizeof(sMPEG2_TS);	///< jump across (MPEG2-TS HDR+payload(184 bytes)),point to the next MPEG2-TS HDR
		curr_ptr   += sizeof(sMPEG2_TS);
		
	} while(bytes_left > 0);	
	
	return PKT_TYPE_NULL;	///< Distinguish in the end,indicate to free the descriptor
}


/* -------------------------------------------------
/       pkt_process_thread()
/              Distinguish what kinds of packet(pcr,video,audio) and push them into each queue
/---------------------------------------------------*/
static void pkt_process_thread(void *arg){
	
	SX_QUEUE	*queue;
	sSX_DESC	*desc;
	sSX_DESC	*h264_desc;
	sSX_DESC	*lpcm_desc;
	UINT32		bytes_left;

	while(1){
	
		do{
			desc = sx_pipe_get(SX_VRDMA_PKT_QUEUE);	///< dequeue
			if(desc == NULL){
				break;
			}
			
			/* Get pkt length */
			bytes_left = desc->data_len;	///< bytes_left = rtp_hdr + rtp_payload
			assert(bytes_left > sizeof(sRTP_HDR));	
	
			/* Get and push program reference time to SX_VRDMA_PCR queue */
			UINT64 pcr_ms = pcr_get(desc);
			if(pcr_ms > 0){
				
				sSX_DESC	*new_desc = sx_desc_get();	///< get a new pcr_node
				sSLICE_HDR	*hdr = malloc(sizeof(sSLICE_HDR));
				assert(hdr != NULL);

				hdr->type 		= SLICE_TYPE_PCR;  ///< TODO 
				hdr->timestamp 		= pcr_ms;
				
				new_desc->data  	= (void *) hdr;
				new_desc->data_len 	= sizeof(sSLICE_HDR);
		
				sx_pipe_put(SX_VRDMA_PCR, new_desc);	///< enqueue
			}
			
			/* Distinguish and push media packet into each Queue */
			ePKT_TYPE	pkt_type = pkt_type_get(desc);
			switch(pkt_type){
				
				case PKT_TYPE_VIDEO:
				{
					sx_pipe_put(SX_VRDMA_VIDEO_PKT_QUEUE, desc);
					break;
				}
				case PKT_TYPE_AUDIO:
				{
					sx_pipe_put(SX_VRDMA_AUDIO_PKT_QUEUE, desc);
					break;
				}
				case PKT_TYPE_NULL:
				{
					sx_desc_put(desc);	///< Distinguish in the end,just free the descriptor
					break;
				}
				default:
				{
					assert(0);
					break;
				}

			}
		} while(1);
		
		usleep(1*1000);
	}
}

/* -------------------------------------------------
/
       sx_mgmt_m2ts_decoder_open() 
/              Create a pthread to process pkt from SX_VRDMA_PKT_QUEUE 
/---------------------------------------------------*/
void sx_mgmt_m2ts_decoder_open(void){

	sx_thread_create(&f_cblk.pkt_process_thread, &pkt_process_thread, NULL, MGMT_M2TS_PKT_PROCESS_THREAD_PRIORITY);
	printf("(mgmt_m2ts_decoder_open): sx_mgmt_m2ts_decoder_open(): pthread create,Invoked\n");

}
