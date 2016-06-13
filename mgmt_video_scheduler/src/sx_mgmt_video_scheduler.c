#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <assert.h>
#include <errno.h>
#include <sys/time.h>

#include "sx_system.h"
#include "sx_pkt.h"
#include "sx_desc.h"
#include "sx_pipe.h"
#include "sx_thread_priority.h"
#include "sx_video_sink.h"


/* -------------------------------------------------
/       sMGMT_VIDEO_SCHEDULER_CBLK 
/              the object of sMGMT_VIDEO_SCHEDULER_control_block which owns 3 thread id,pthread_mutex_lock and record current time
/---------------------------------------------------*/
typedef struct{

	pthread_t		video_scheduler_thread;
	pthread_t		slice_packing_thread;
	pthread_t		pcr_update_thread;
	UINT64			curr_time;
	pthread_mutex_t		lock;

} sMGMT_VIDEO_SCHEDULER_CBLK;

static sMGMT_VIDEO_SCHEDULER_CBLK	f_cblk;



/* -------------------------------------------------
/       sx_mgmt_video_scheduler_init() 
/              Init video demux process which be implemented by OpenMax ARCH & API, Init_pthread_mutex_lock
/---------------------------------------------------*/
void sx_mgmt_video_scheduler_init(void){
	
	sx_video_sink_init();
	
	pthread_mutex_init(&f_cblk.lock, NULL);

}

/* -------------------------------------------------
/       video_scheduler_slice_drump() 
/              
/---------------------------------------------------*/
static void video_scheduler_slice_dump(
	sSX_DESC	*slice_head
){
	UINT8		*curr_ptr;
	UINT32		bytes_left;
	UINT32		afc;
	//UINT32		pes_byte_count;		///< not used
	UINT32		payload_size;
	UINT32		start_offset;
	UINT32		copy_index;
	UINT32		pid;
	sSX_DESC	*curr_desc;
	sSX_DESC	*head_desc;

	/* Consistency check */
	assert(slice_head);
	
	/* Get descriptor,use this to represent it's hw_buffer_desc */	
	sSX_DESC	*hw_desc = sx_desc_get();	

	/* Set this as another chain */
	sSX_DESC	*hw_desc_head = hw_desc;
	
	/* Get a hw buffer */
	sDECODER_HW_BUFFER	* hw_buf = sx_video_sink_buf_get();
	assert(hw_buf != NULL);
	
	/* HW_Descriptor data is the hw_buf data */
	hw_desc->data = (UINT8 *) hw_buf;
			
	/* Get first descriptor */
	curr_desc = slice_head;
	
	copy_index = 0;		///< later use to record how many bytes we copy from video slice payload to hw_buffer
	
	do{
		/* SLICE head next point to skice pkt data */
		curr_desc = curr_desc->next;

		/* Get current pkt data */
		curr_ptr = curr_desc->data;		

		/* Get packet length */
		bytes_left = curr_desc->data_len;
		assert(bytes_left > sizeof(sRTP_HDR));

		/* GET MPEG2-TS Header */
		curr_ptr	+= sizeof(sRTP_HDR);   ///< point to the RTP payload initial address
		bytes_left 	-= sizeof(sRTP_HDR);
		assert((bytes_left % sizeof(sMPEG2_TS)) == 0);
		
		/*Not used, Reset pes_byte_count */
		//pes_byte_count = 0;
				
		do{
			sMPEG2_TS *ts = (sMPEG2_TS *) curr_ptr;
			afc 	      = AFF_PF_GET(ts->hdr);
			pid	      = PID_GET(ts->hdr);

			if(pid == 0x1011){	/* Check wfd_video_pid */
			
				UINT8	stuffing = 0;
				if(afc & 0x02){		///< flag of (adaptation_field,payload) = (00,01,10,11)
					
					/* Determine how many bytes later to skip */
					stuffing = 1 + ts->payload.payload[0];	///< adaptation field length(1 byte) + its value

				}
				start_offset = stuffing;
				
				if(PUSI_GET(ts->hdr)){	/* Check PUSI(Payload Uint Start Indicator) */
				/* It's because that this pkt is the PES Uint Start(which including (PES_HDR + OPT_PES_HDR) = 14bytes + PES_PAYLOAD)      
                                                 24bits             8bits        16bits
                                 PES_HDR-> |packet start code prefex|stream id|PES packet length|
                                           -----------------------------------------------------
                                                2        2               1       1               1         1             8bits      8bits        40bits
                                 OPT_PES_HDR-> |10|PES scrambling|PES priority|data alignment|copyright|original or|7 flags(PTS)|PES header |optional fields|           
                                               |  |      control |            |    indicator |         |   copy    |            |data length| PTS(48 bits)  |
                                 */
						
					start_offset += 14;
				}
				
				payload_size = sizeof(sMPEG2_TS_PAYLOAD) - start_offset;

				if((copy_index + payload_size) > 81920){	///< Check whether if there is enough hw buffer size(81920 = 80kbytes)
					
					/* If the hw buffer is full, just submit the current buffer and point next to the new hw buffer */
					hw_buf->buffer_len = copy_index;
					
					/* Get a new hw buffer desc */
					hw_desc->next = sx_desc_get();

					/* Point to the new descriptor */
					hw_desc = hw_desc->next;
		
					/* Get a new hw buffer */
					hw_buf = sx_video_sink_buf_get();
					assert(hw_buf != NULL);

					/* HW_Descriptor data is the hw_buf data */
					hw_desc->data = (UINT8 *) hw_buf;
				
					/* Reset index */					
					copy_index = 0;
				}
				
				/* Memory Copy from SLICE pkt to hw buffer */
				memcpy(&hw_buf->buffer[copy_index],		
				       &ts->payload.payload[start_offset],
				       payload_size);	
				
				/* Record how many bytes has copyed */
				copy_index += payload_size;	
			}

			curr_ptr   += sizeof(sMPEG2_TS);
			bytes_left -= sizeof(sMPEG2_TS);

		} while(bytes_left > 0);
	
	
	} while(curr_desc->next != NULL);

	/* Set total length about copy_index to record hw buffer length*/	
	hw_buf->buffer_len = copy_index;

	/* Replace the existing slice chain which will be released with hw_buffer ,and push them into SLICE_READY queue*/	
	/* Free the existing slice chain(only payload) ,don't including the SLICE HDR(has PTS)*/
	sx_desc_put(slice_head->next);	
		
	/* SLICE HDR next point to SLICE chain head */
	slice_head->next = hw_desc_head;

	sx_pipe_put(SX_VRDMA_SLICE_READY, slice_head);
}

/* -------------------------------------------------
/       slice_packing_thread() 
/              
/---------------------------------------------------*/
void slice_packing_thread(void *arg){
	
	sSX_DESC	*desc = NULL;
	while(1){
		UINT32	len = sx_pipe_len_get(SX_VRDMA_SLICE_READY);
		if(len >= 10){
			/* more than enough. Try again next iteration 
			   Because limit the size of HW buffers, if too much SLICE in the SX_VRDMA_SLICE_READY queue are to render,
			   suspend the slice_packing,do not push the SLICE into SX_VRDMA_SLICE_READY queue
			*/
			goto next_iter;
		}
		/* Check slice_to_dump equal or lower than ten */
		UINT32	slice_to_dump = 10 - len;
		do{
			desc = sx_pipe_get(SX_VRDMA_SLICE);
			if(desc == NULL){

				goto next_iter;
			}
			/* Dump the slice,[Dump] means that

			 */
			video_scheduler_slice_dump(desc);

			slice_to_dump --;

		} while(slice_to_dump > 0);

	next_iter:
		usleep(2 * 1000);
	}

}

/* -------------------------------------------------
/       sink_time_get() 
/		TODO need to figure out how to calculate time      
/---------------------------------------------------*/
static UINT64 sink_time_get(void){
	struct timeval 		curr_time;
	
	/* Get current time */
	gettimeofday(&curr_time, NULL);

	UINT64 temp = curr_time.tv_sec * 1000 + curr_time.tv_usec /1000;
	
	return temp;
}


/* -------------------------------------------------
/       pcr_update_thread() 
/		TODO need to figure out how to calculate time      
/---------------------------------------------------*/
static void pcr_update_thread(void *arg){
	sSX_DESC	*desc;
	UINT64		pcr_time;
	UINT64		pcr_received_time;
	UINT64		curr_time;

	while(1){
		desc = sx_pipe_get(SX_VRDMA_PCR);
		if(desc == NULL){
			goto cleanup;
		}			

		sSLICE_HDR	*hdr = (sSLICE_HDR *) desc->data;
		
		/* update PCR time */
		pcr_time = hdr->timestamp;
		
		/* Free descriptor */
		sx_desc_put(desc);

		/* Cache received time */
		pcr_received_time = sink_time_get();

cleanup:
		/* Get current time */
		curr_time = sink_time_get();

		pthread_mutex_lock(&f_cblk.lock);

		/* TODO how to calculate curr_time,SX_SYSTEM_DELAY_MS = (100),SX_SYSTEM_AUDIO_SOURCE_DELAY_MS = (400) */
		f_cblk.curr_time = pcr_time + (curr_time - pcr_received_time) - SX_SYSTEM_AUDIO_SOURCE_DELAY_MS - SX_SYSTEM_DELAY_MS;	

		pthread_mutex_unlock(&f_cblk.lock);
			
		usleep(2 * 1000);
	}
}

/* -------------------------------------------------
/       estimated_source_time_get() 
/		Use mutex lock to prevent synchronous problem.and then get f_cblk.curr_time              
/---------------------------------------------------*/
static UINT64 estimated_source_time_get(void){

	UINT64	time;
	
	pthread_mutex_lock(&f_cblk.lock);
	
	time = f_cblk.curr_time;	

	pthread_mutex_unlock(&f_cblk.lock);

	return time;
}



/* -------------------------------------------------
/       video_scheduler_thread() 
/              
/---------------------------------------------------*/
static void video_scheduler_thread(void *arg){
	sSX_DESC	*desc = NULL;
	UINT64		slice_present_time;

	while(1){

		while(1){
		
			/* Get hw_buffere from SLICE_READY queue */
			if(desc == NULL){
				desc = sx_pipe_get(SX_VRDMA_SLICE_READY);

				if(desc == NULL){
					goto next_iter;
				}
			}

			sSLICE_HDR *hdr = (sSLICE_HDR *) desc->data;

			/* Get PTS */
			slice_present_time = ((sSLICE_HDR *) desc->data)->timestamp;
		
			/* TODO  */	
			UINT64 estimated_source_time = estimated_source_time_get();
			UINT8  present	= (estimated_source_time > slice_present_time ) ? 1:0;

			if(!present){
				goto next_iter;
			}

			sSX_DESC *curr = desc->next;
			sSX_DESC *next;
			do{
				/* set the hw_buffer(which has READY SLICE) to video render component */	
				sx_video_sink_buf_set((sDECODER_HW_BUFFER *) curr->data);
				next = curr->next;
			
				free(curr);

				curr = next;

			} while(curr != NULL);

			free(desc->data);
			free(desc);

			/* Set descriptor pointer to NULL */
			desc = NULL;
		}
next_iter:
		usleep(1*1000);
	}
	
} 


/* -------------------------------------------------
/       sx_mgmt_video_scheduler_open() 
/              Create three pthreads(slice_packing,pcr_update,video_scheduler)
/---------------------------------------------------*/
void sx_mgmt_video_scheduler_open(void){
	
	sx_thread_create(&f_cblk.video_scheduler_thread, ///< Dependent with slice_packing_thread -> get SX_VRDMA_SLICE_READY	
			 &video_scheduler_thread,        ///< Dependent with pcr_update_thread    -> estimate time		
			 NULL,
			 VIDEO_SCHEDULER_THREAD_PRIORITY);
	
	sx_thread_create(&f_cblk.slice_packing_thread,	///< Independent,Check SX_VRDMA_SLICE_READY queue_len, get SX_VRDMA_SLICE and push into SLICE_READY queue  
			 &slice_packing_thread,
			 NULL,
			 VIDEO_SCHEDULER_THREAD_PRIORITY);

	sx_thread_create(&f_cblk.pcr_update_thread,	///< Independent,Check SX_VRDMA_PCR queue
			 &pcr_update_thread,
			 NULL,
			 VIDEO_SCHEDULER_THREAD_PRIORITY);
}
