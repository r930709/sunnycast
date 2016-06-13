#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <netinet/in.h>
#include <assert.h>
#include <errno.h>
#include <sys/time.h>


#include "sx_pkt.h"
#include "sx_desc.h"
#include "sx_pipe.h"
#include "sx_thread_priority.h"
#include "sx_audio_sink.h"
#include "sx_types.h"


/* -------------------------------------------------
/       sSLICE_CHAIN 
/              SLICE CHAIN Recorder which record SLICE CHAIN head/tail address
/---------------------------------------------------*/
typedef struct{
	sSX_DESC	*head;
	sSX_DESC	*tail;
	
} sSLICE_CHAIN;

/* -------------------------------------------------
/       sDECODER_CBLK
/              the object of sDECODER_control_block which owns information about audio decoder
/---------------------------------------------------*/
typedef struct{

	pthread_t	decoder_thread;
	UINT32		look_for_new_slice;		///< it means re-orginize the slice
	UINT32		continue_current_slice;		///< it means at this time period,try to organize the current slice
	UINT32		pes_len;	
	UINT32		pes_curr_byte_count;		///< it means to record current PES payload length,it will accumulate the parse PES payload
	UINT32		last_seq_num;
	sSLICE_CHAIN	slice_chain;
} sDECODER_CBLK;

static sDECODER_CBLK	f_cblk;

/* -------------------------------------------------
/       sx_mgmt_audio_decoder_init()
/              Init audio decoder
/---------------------------------------------------*/
void sx_mgmt_audio_decoder_init(void){

	/* Set look for new slice  */
	f_cblk.look_for_new_slice = 1;

	/* Init audio decoder hardware  */
	sx_audio_sink_init();
}

/* -------------------------------------------------
/       slice_start_find()
/              
/---------------------------------------------------*/
static UINT8 slice_start_find(
	sSX_DESC	*desc,
	UINT32		*pes_payload_size
){
	UINT8		*curr_ptr;
	UINT32		bytes_left;
	UINT32		pid;
	//UINT32		afc;	///< TODO Not Used


	/*Get current pkt data */
	curr_ptr = desc->data;
			
	/* Get packet length */
	bytes_left = desc->data_len;
	assert(bytes_left > sizeof(sRTP_HDR));
			
	/* GET MPEG2-TS Header */
	curr_ptr   += sizeof(sRTP_HDR);			

	/* Get MPEG2-TS bytes left */
	bytes_left -= sizeof(sRTP_HDR);
	assert((bytes_left) % sizeof(sMPEG2_TS) == 0);	

	do{
		sMPEG2_TS *ts = (sMPEG2_TS *) curr_ptr;

		//afc = AFF_PF_GET(ts->hdr);	///< TODO Not used,adpation field flag & payload flag
		pid = PID_GET(ts->hdr);

		if(PUSI_GET(ts->hdr) && (pid == 0x1100)){	/* Check PUSI(Payload Uint Start Indicator) & wfd_audio_pid */
		
			curr_ptr = &ts->payload.payload[0];

			curr_ptr += sizeof(sPES);

			sPES_EXT *pes_ext = (sPES_EXT *) curr_ptr;

			*pes_payload_size =  ntohs(pes_ext->length) - 14;	///< Get a complete total PES Payload which cut off PES HDR
			
			/* TODO need to find why 1920 && -14 */
			assert((*pes_payload_size == 1920) || (*pes_payload_size == -14));

			return 1;

		}
		
		curr_ptr   += sizeof(sMPEG2_TS);
		bytes_left -= sizeof(sMPEG2_TS);
	
	} while(bytes_left > 0);

	return 0;
}

/* -------------------------------------------------
/       pes_payload_size()
/              
/---------------------------------------------------*/
static UINT32 pes_payload_size(
	sSX_DESC	*desc
){
	UINT8	*curr_ptr;
	UINT32	bytes_left;
	UINT32	afc;
	UINT32	pid;
	UINT32	pes_byte_count;	///< it means to record current PES payload length,it will accumulate the parse PES payload
	UINT32	payload_size;	///< it means to record current MPEG2-TS of PES payload size	

	
	/*Get current pkt data */
	curr_ptr = desc->data;
			
	/* Get packet length */
	bytes_left = desc->data_len;
	assert(bytes_left > sizeof(sRTP_HDR));
			
	/* GET MPEG2-TS Header */
	curr_ptr   += sizeof(sRTP_HDR);			

	/* Get MPEG2-TS bytes left */
	bytes_left -= sizeof(sRTP_HDR);
	assert((bytes_left) % sizeof(sMPEG2_TS) == 0);	

	pes_byte_count = 0;
	do{
		sMPEG2_TS	*ts = (sMPEG2_TS *) curr_ptr;
		
		afc = AFF_PF_GET(ts->hdr);
		pid = PID_GET(ts->hdr);

		if(pid == 0x1100){
			
			if(PUSI_GET(ts->hdr)){

				/*TODO check whether MPEG2-TS PUSI packet has adaptation field */
				assert((afc == 0x01) ||(afc == 0x03));

				/*TODO check why different from video decoder, cut 14 */
				payload_size = (sizeof(sMPEG2_TS_PAYLOAD) - 20);
			}else{
				if(afc == 0x01){
				
					payload_size = sizeof(sMPEG2_TS_PAYLOAD);
				}else if(afc == 0x03){

					payload_size = sizeof(sMPEG2_TS_PAYLOAD) - 1 - ts->payload.payload[0];   ///< ignore adaptation field length and itseslf  
				}else{
					assert(0);
				}
			}
		pes_byte_count += payload_size;

		}
		curr_ptr   += sizeof(sMPEG2_TS);
		bytes_left -= sizeof(sMPEG2_TS);

	} while(bytes_left > 0);

	return pes_byte_count;
}

/* -------------------------------------------------
/       slice_pkt_add() 
/               Judge 2 situations(empty/already one slice packet),and connect linklist,and update head/tail pointer address
/---------------------------------------------------*/
static void slice_pkt_add(
        sSX_DESC        *desc
){
        assert(desc != NULL);
            
        /* Check whether if it is the first slice packet */
        if(f_cblk.slice_chain.head == NULL){
                    
                f_cblk.slice_chain.head = desc;
                f_cblk.slice_chain.tail = desc;
                return;
        }   
            
        /* There is already one slice,connect to become its next slice packet */
        f_cblk.slice_chain.tail->next = desc;               
            
        /* Update tail */
        f_cblk.slice_chain.tail = desc;
}

/* -------------------------------------------------
/       slice_drop() 
/               Judge 2 situations(empty/already one slice packet),and connect linklist,and update head/tail pointer address              
/---------------------------------------------------*/
static void slice_drop(void){
        
        sSX_DESC        *curr;
        sSX_DESC        *next;

        /* Drop slice ,Release desc address from head to tail */
        sx_desc_put(f_cblk.slice_chain.head);
        
        /* Reset head and tail */
        f_cblk.slice_chain.head = NULL;
        f_cblk.slice_chain.tail = NULL;

        printf("slice dropped() invoked\n");
}

/* -------------------------------------------------
/       slice_get() 
/               Get the Slice chain head and then return it
/---------------------------------------------------*/
static sSX_DESC *slice_get(void){

        sSX_DESC        *head;
        head = f_cblk.slice_chain.head;

        f_cblk.slice_chain.head = NULL;
        f_cblk.slice_chain.tail = NULL;

        return head;

}     

/* -------------------------------------------------
/       audio_decoder_slice_dump() 
/ 		TODO why it different from video_decoder_slice_dump(Get the slice chain head, patch a SLICE HDR in front of SLICE chain head) 
/---------------------------------------------------*/
static void audio_decoder_slice_dump(
	sSX_DESC	*slice_head
){
	sx_pipe_put(SX_VRDMA_LPCM_SLICE, slice_head);	

}


/* -------------------------------------------------
/       audio_decoder_thread()
/              
/---------------------------------------------------*/
static void audio_decoder_thread(void *arg){
	sSX_DESC	*desc;
	UINT8		*curr_ptr;
	UINT32		bytes_left;
	sMPEG2_TS	*ts;
	
	while(1){

		do{

			desc = sx_pipe_get(SX_VRDMA_LPCM);
			if(desc == NULL){
				break;
			}

			/*Get current pkt data */
			curr_ptr = desc->data;
			
			/* Get packet length */
			bytes_left = desc->data_len;
			assert(bytes_left > sizeof(sRTP_HDR));
			
			/* GET RTP Header */
			sRTP_HDR *rtp_hdr = (sRTP_HDR *) curr_ptr;			
		
			if(f_cblk.look_for_new_slice){

				/* Check whether find the slice start ,and record the complete PES payload length which cut off PES HDR */
				if(slice_start_find(desc, &f_cblk.pes_len)){
		
					f_cblk.look_for_new_slice = 0;

					/* Record now organize a slice */
					f_cblk.continue_current_slice = 1;		

					/* Reset current PES payload length */
					f_cblk.pes_curr_byte_count = 0;

					f_cblk.last_seq_num = ntohs(rtp_hdr->sequence_num) - 1;
				}
			}
			
			/* Check are we orginize a slice now */	
			if(f_cblk.continue_current_slice){
				
				/* TODO different from video_decoder_thread which use MPEG2-TS CC to check whether loss pkt */
				if(ntohs(rtp_hdr->sequence_num) != (f_cblk.last_seq_num + 1)){
				
					/* If loss pkt,and then give up this slice,reset the related param */
					slice_drop();

					f_cblk.continue_current_slice = 0;
					f_cblk.look_for_new_slice = 1;

				}else{
					
					f_cblk.last_seq_num = ntohs(rtp_hdr->sequence_num);
					
					/* Record the current PES payload size */					
					UINT32 pes_size = pes_payload_size(desc);

					/* Accumulate the current PES payload size to check whether if achive a complete PES payload size */
					f_cblk.pes_curr_byte_count += pes_size;
					assert(f_cblk.pes_curr_byte_count <= f_cblk.pes_len);
					
					/* Originize the Slice chain(link-list) ,add slice into slice chain,so plus the slice_pkt */
					slice_pkt_add(desc);
					
					/* If achive a complete PES payload size,dump the slice chain ,and reset related param */		
					if(f_cblk.pes_curr_byte_count == f_cblk.pes_len){
						
						f_cblk.continue_current_slice = 0;
						f_cblk.look_for_new_slice = 1;

						audio_decoder_slice_dump(slice_get());
					}
				}
			}

		} while(1);
			
		usleep(1000);
	}
}


/* -------------------------------------------------
/       sx_mgmt_audio_decoder_open()
/              Create a pthread to decode audio pkt
/---------------------------------------------------*/
void sx_mgmt_audio_decoder_open(void){

	sx_thread_create(&f_cblk.decoder_thread, &audio_decoder_thread, NULL, AUDIO_DECODER_THREAD_PRIORITY);
}
