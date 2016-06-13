#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <assert.h>
#include <errno.h>
#include <sys/time.h>

#include "sx_pkt.h"
#include "sx_desc.h"
#include "sx_pipe.h"
#include "sx_thread_priority.h"
#include "sx_audio_sink.h"
#include "sx_types.h"
#include "sx_system.h"


/* -------------------------------------------------
/       eSTATE 
/              enum state inactive/active => 0/1
/---------------------------------------------------*/
typedef enum{
	STATE_INACTIVEm,
	STATE_ACTIVE	
	
} eSTATE;

/* -------------------------------------------------
/       sMGMT_AUDIO_SCHEDULER_CBLK
/              the object of sMGMT_AUDIO_SCHEDULER_control_block which owns information about audio scheduler
/---------------------------------------------------*/
typedef struct{

	pthread_t	audio_scheduler_thread;
	eSTATE		state;

} sMGMT_AUDIO_SCHEDULER_CBLK;

static sMGMT_AUDIO_SCHEDULER_CBLK	f_cblk;

/* -------------------------------------------------
/       sx_mgmt_audio_scheduler_init()
/              Init audio scheduler
/---------------------------------------------------*/
void sx_mgmt_audio_scheduler_init(void){


	/*TODO sx_mgmt_audio_decoder_init() also do sink_init(), seems don't need to do it again ,Init audio scheduler hardware  */
	sx_audio_sink_init();

	printf("(audio_scheduler): Init\n");
}

/* -------------------------------------------------
/       audio_total_remaining_ms()
/              TODO why??
/---------------------------------------------------*/
static UINT32 audio_total_remaining_ms(void){
	
	UINT32	data_left = sx_pipe_len_get(SX_VRDMA_LPCM_SLICE) * 10;
	
	UINT32	queued_left = sx_audio_sink_ms_left_get();

	return (data_left + queued_left);

}

/* -------------------------------------------------
/       audio_endianness_convert()
/              
/---------------------------------------------------*/
static void inline audio_endianness_convert(
	UINT16	*temp,
	UINT32	samples
){
	UINT32	i;
	
	for(i =0; i < samples; i++){
		
		temp[i] = ntohs(temp[i]);
	}
}

/* -------------------------------------------------
/       audio_scheduler_slice_dump()
/              
/---------------------------------------------------*/
static void audio_scheduler_slice_dump(
	sSX_DESC	*slice_head
){
	UINT8		*curr_ptr;
	UINT32		bytes_left;
	UINT32		afc;
	UINT32		pid;
	UINT32		pes_byte_count;
	UINT32		payload_size;
	UINT32		start_offset;
	UINT32		copy_index;
	UINT32		samples_left;
	UINT32		ms_left;
	sSX_DESC	*desc;

	static UINT8	playback_speed = 1;

	ms_left = audio_total_remaining_ms();
	
	/* TODO  why? */
	if(ms_left > (100 + SX_SYSTEM_DELAY_MS))	///< SX_SYSTEM_DELAY_MS = 100
	{
		if(playback_speed != 2){
					
			sx_audio_sink_playback_speed_inc();

			playback_speed = 2;
		}
	}
	/* TODO  why? */
	else if(ms_left < (50 + SX_SYSTEM_DELAY_MS))	///< SX_SYSTEM_DELAY_MS = 100
	{
		if(playback_speed != 0){
					
			sx_audio_sink_playback_speed_dec();

			playback_speed = 0;
		}
	}
	/* TODO  why? */
	else{	///< SX_SYSTEM_DELAY_MS = 100
	
		if(playback_speed != 1){
					
			sx_audio_sink_playback_speed_reset();

			playback_speed = 1;
		}
	}

	/*  */
	UINT8 *buf = sx_audio_sink_buffer_get();
	
	/* */
	copy_index = 0;

	desc = slice_head;
	do{
		
                /* Get current pkt data */
                curr_ptr = desc->data;                 

                /* Get packet length */
                bytes_left = desc->data_len;
                assert(bytes_left > sizeof(sRTP_HDR));

                /* GET MPEG2-TS Header */
                curr_ptr        += sizeof(sRTP_HDR);   ///< point to the RTP payload initial address
                bytes_left      -= sizeof(sRTP_HDR);
                assert((bytes_left % sizeof(sMPEG2_TS)) == 0);                                                                                                                               
                    
                /* Reset pes_byte_count */
                pes_byte_count = 0;
		do{
			sMPEG2_TS *ts = (sMPEG2_TS *) curr_ptr;
		
			afc = AFF_PF_GET(ts->hdr);
			pid = PID_GET(ts->hdr);

			if(pid == 0x1100){	/* Check wfd_audio_pid */

				UINT8 stuffing = 0;
				if(afc & 0x02){		///< flag of (adaptation_field,payload) = (00,01,10,11)

					/* Determine how many bytes later to skip */
					stuffing = 1 + ts->payload.payload[0]; 	///< adaptation field length(1 byte) + its value
				}
				start_offset = stuffing;
				
				if(PUSI_GET(ts->hdr)){ /*TODO why not 14 is 20?!, Check PUSI(Payload Uint Start Indicator) */
                                /* It's because that this pkt is the PES Uint Start(which including (PES_HDR + OPT_PES_HDR) = 14bytes + PES_PAYLOAD)      
                                                 24bits             8bits        16bits
                                 PES_HDR-> |packet start code prefex|stream id|PES packet length|
                                           -----------------------------------------------------
                                                2        2               1       1               1         1             8bits      8bits        40bits
                                 OPT_PES_HDR-> |10|PES scrambling|PES priority|data alignment|copyright|original or|7 flags(PTS)|PES header |optional fields|           
                                               |  |      control |            |    indicator |         |   copy    |            |data length| PTS(48 bits)  |
                                 */

					
					start_offset += 20;
				}
				payload_size = sizeof(sMPEG2_TS_PAYLOAD) - start_offset;

				/* Memory Copy from SLICE pkt to hw buffer */	
				memcpy(&buf[copy_index],
					&ts->payload.payload[start_offset],
					payload_size);

				/* Record how many bytes has copyed */
				copy_index += payload_size;
			}

			curr_ptr   += sizeof(sMPEG2_TS);
			bytes_left -= sizeof(sMPEG2_TS);

		} while(bytes_left > 0);

	  	desc = desc->next;
	} while(desc != NULL);

	sx_desc_put(slice_head);
	
	/* TODO why is 1920 ,static size */
	assert(copy_index == 1920);	

	/* TODO buf why need to be cast UIN16 not UINT8 */
	audio_endianness_convert((UINT16 *) buf,960);
	
	/* Push to decoder hardware */
	sx_audio_sink_buffer_set(buf,1920);
}


/* -------------------------------------------------
/       audio_scheduler_thread()
/              
/---------------------------------------------------*/
static void audio_scheduler_thread(void *arg){
	sSX_DESC	*desc;

	while(1){
		if(f_cblk.state == STATE_INACTIVE){
	
			UINT32	len = sx_pipe_len_get(SX_VRDMA_LPCM_SLICE);
			
			/*  */
			if(len > (SX_SYSTEM_DELAY_MS / 10)){	///< SX_SYSTEM_DELAY_MS	= 100
				
				f_cblk.state = STATE_ACTIVE;
				printf("(audio_scheduler): Transition to active. [len = %u]\n",len);
				
				goto next_iter;
			}
		}else{
		
			/* TODO why?? same as audio_total_remaining_ms()  */
			UINT32	data_left_ms = sx_pipe_len_get(SX_VRDMA_LPCM_SLICE) * 10;	

			/*  */
			UINT32	queued_ms = sx_audio_sink_ms_left_get();
				
			if((data_left_ms + queued_ms) == 0){
		
				f_cblk.state = STATE_INACTIVE;
				printf("(audio_scheduler): Transition to inactive\n");
				goto next_iter;
			}
			
			/* TODO why?? */	
			if(queued_ms < 200){
				UINT32	 slices_to_queue = (200 - queued_ms + 10) / 10;
				do{
					/* Get ready slice */
					desc = sx_pipe_get(SX_VRDMA_LPCM_SLICE);
					if(desc == NULL){
						goto next_iter;
					}	

					/* Dump slice */
					audio_scheduler_slice_dump(desc);

					slices_to_queue --;
				} while(slices_to_queue > 0);

			}
		}

	next_iter:
		usleep(5 * 1000);
	}

}


/* -------------------------------------------------
/       sx_mgmt_audio_scheduler_open()
/              Create a pthread 
/---------------------------------------------------*/
void sx_mgmt_audio_scheduler_open(void){


	sx_thread_create(&f_cblk.audio_scheduler_thread, &audio_scheduler_thread, NULL, AUDIO_SCHEDULER_THREAD_PRIORITY);
}
