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
/              the object of sDECODER_control_block which owns information about video decoder
/---------------------------------------------------*/
typedef struct{
	
	pthread_t	decoder_thread;
	UINT32		look_for_new_slice;
	UINT32		pes_payload_curr_len;
	UINT32		last_cc;
	UINT32		slice_pkt_count;
	UINT64		slice_pts_ms;		///< slice present timestamp(mili second)
	UINT64		slice_count;
	sSLICE_CHAIN	slice_chain;
} sDECODER_CBLK;

static sDECODER_CBLK	f_cblk;

/* -------------------------------------------------
/       slice_start_find() 
/		Continue to search each MPEG2-TS pkt,until find the video payload uint start indicator.
/		Record pes_payload_size,pts_ms              
/---------------------------------------------------*/
static UINT8 slice_start_find(
	sSX_DESC	*desc,
	UINT32		*pes_payload_size,
	UINT64		*pts_ms
){
	UINT8		*curr_ptr;
	UINT32		bytes_left;
	sMPEG2_TS	*ts;
	UINT32		pid;
	UINT32		afc;

	/* TODO repeat shift behavior */
	/*Get current pkt data */
	curr_ptr = desc->data;
		
	/* Get packet length */
	bytes_left = desc->data_len;
	assert(bytes_left > sizeof(sRTP_HDR));
		
	/* GET MPEG2-TS Header */
	curr_ptr += sizeof(sRTP_HDR);   ///< point to the RTP payload initial address

	/* GET MPEG2-TS header+payload bytes(size of packet left) */
	bytes_left -= sizeof(sRTP_HDR);
	assert((bytes_left % sizeof(sMPEG2_TS)) == 0);  ///< MPEG2-TS HDR + payload(184 bytes)

	do{
		sMPEG2_TS	*ts = (sMPEG2_TS *) curr_ptr;

		afc = AFF_PF_GET(ts->hdr);	///< adpation field flag & payload flag
		pid = PID_GET(ts->hdr);		

		if(PUSI_GET(ts->hdr) && (pid == 0x1011)){	/* Check PUSI(Payload Uint Start Indicator) & wfd_video_pid */
			
			curr_ptr = &ts->payload.payload[0];	///< point to the mpeg2-ts payload(including adaptation field) 
								///< init addr(next continuity counter is adaptation field length)
			
			/* Check whether if adaptation field is*/
			UINT8 adaptation_field_len = 0;
			if(afc & 0x02){		///< flag of (adaptation_field,payload) = (00,01,10,11)	
			
				/* Determine how many bytes later to skip */			
				adaptation_field_len = 1 + *curr_ptr;	///< adaptation field length(1 byte) + its value
			}
			/* Skip adaptation filed length ,point to the payload */
			curr_ptr += adaptation_field_len;	
			
			/* TODO May be wrong, Skip PES HDR_base ,point to the PES_EXT,get the PES packet length */
			curr_ptr += sizeof(sPES);

			sPES_EXT *pes_ext = (sPES_EXT *) curr_ptr;
			UINT16	 len 	  = ntohs(pes_ext->length);
			if(len > 0){
				*pes_payload_size = len - 8;	///< TODO Maybe wrong, Skip 8 bytes(sPES+sPES_EXT)=(PES HDR) 
			}else{
				*pes_payload_size = 0;
			}

			/* jump across sPES_EXT & sPES_EXT2 ,point to the PTS(presentation timestamp)*/
			curr_ptr += sizeof(sPES_EXT);
			curr_ptr ++; 			

			/* Get pts(5bytes,Big Endian) */
			UINT32	i;
			UINT64	pts = 0;
			for(i = 0; i<5 ; i++){
				pts = ((pts << 8) | curr_ptr[i]);
			}
			
			/* Transform uint from freq to time  
			/	7654  3	2   1	0 76543210 7654321 0 76543210 7654321 0
			/      +-----------------+------------------+------------------+
			/      |0010|PTS 32..30|1|PTS 29.. 15	  |1|PTS 14 .. 00    |1|	
			/	PTS Total 33 bits					*/
			UINT64	pts_hz = 0;
			UINT64	mask;
			
			mask = 0x0007;
			pts_hz |= (pts & (mask << 33)) >> 3;
			
			mask = 0x7fff;
			pts_hz |= (pts & (mask << 17)) >> 2;
			
			mask = 0x7fff;
			pts_hz |= (pts & (mask << 1)) >> 1;
					        	
			*pts_ms = pts_hz/ 90;	///< Convert to ms, PTS has a resolution of 90kHz
			
			return 1;

		}
		
		/* Not yet, continue to find slice start */		
		curr_ptr	+= sizeof(sMPEG2_TS);	///< jump across (MPEG2-TS HDR+payload(184 bytes)),point to the next MPEG2-TS HDR
		bytes_left	-= sizeof(sMPEG2_TS);	

	} while(bytes_left > 0);
	
	return 0;	
}

/* -------------------------------------------------
/       slice_get() 
/		Get the Slice chain head and then return it
/---------------------------------------------------*/
static sSX_DESC *slice_chain_get(void){
	
	sSX_DESC	*head;
	head = f_cblk.slice_chain.head;
	
	f_cblk.slice_chain.head = NULL;   
	f_cblk.slice_chain.tail = NULL;

	return head;

}


/* -------------------------------------------------
/       video_decoder_slice_dump() 
/		After get the slice chain head, patch a SLICE HDR in front of SLICE chain head, push these into SLICE Queue 
/---------------------------------------------------*/
static void video_decoder_slice_dump(
	UINT64		pts_ms,
	sSX_DESC	*slice_head
){
	sSLICE_HDR	*hdr = malloc(sizeof(sSLICE_HDR));
	assert(hdr != NULL);

	hdr->type	= SLICE_TYPE_SLICE;
	hdr->timestamp	= pts_ms;

	sSX_DESC	*new_desc = sx_desc_get();
	
	new_desc->data 		= (void *) hdr;
	new_desc->data_len	= sizeof(sSLICE_HDR);
	new_desc->next		= slice_head;	///< SLICE HDR next point to SLICE chain head
	
	sx_pipe_put(SX_VRDMA_SLICE, new_desc); 		

}

/* -------------------------------------------------
/       m2ts_data_interpret()
/		Record first/last MPEG2-TS pkt Continuity counter value ,and temporal total length of PES
/---------------------------------------------------*/
static void m2ts_data_interpret(
	sSX_DESC	*desc,
	UINT32		*pes_payload_size,
	UINT8		*cc_start,
	UINT8		*cc_end
){
	UINT8	*curr_ptr;
	UINT32	bytes_left;
	UINT32	afc;
	UINT32	pid;
	UINT32	pes_byte_count = 0;
	UINT32	payload_size;
	UINT8	cc;	

	
	/*Get current pkt data */
	curr_ptr = desc->data;
		
	/* Get packet length */
	bytes_left = desc->data_len;
	assert(bytes_left > sizeof(sRTP_HDR));
		
	/* GET MPEG2-TS Header */
	curr_ptr 	+= sizeof(sRTP_HDR);   ///< point to the RTP payload initial address
	bytes_left 	-= sizeof(sRTP_HDR);
	assert((bytes_left % sizeof(sMPEG2_TS)) == 0);

	UINT8	first_chunk = 1;
	
	do{
	
		sMPEG2_TS *ts = (sMPEG2_TS *) curr_ptr;
			  afc =	AFF_PF_GET(ts->hdr);
			  pid = PID_GET(ts->hdr);

		if(pid == 0x1011){	/* Check wfd_video_pid */
			
			cc = CC_GET(ts->hdr);
			if(first_chunk){
				
				/* Record first MPEG2-TS pkt Continuity counter value */
				*cc_start   = cc;
				first_chunk = 0;
			}
			if(PUSI_GET(ts->hdr)){	  /* Check PUSI(Payload Uint Start Indicator) */
			
				/* It's because that this pkt is the PES Uint Start(which including (PES_HDR + OPT_PES_HDR) = 14bytes + PES_PAYLOAD) 	  
						24bits		   8bits	16bits
				PES_HDR-> |packet start code prefex|stream id|PES packet length|
				     	  ------------------------------------------------------
					       2	2		1	1		1	  1		8bits	   8bits	40bits
				OPT_PES_HDR-> |10|PES scrambling|PES priority|data alignment|copyright|original or|7 flags(PTS)|PES header |optional fields|		
					      |  |	control	| 	     |	  indicator |	      |   copy    |            |data length| PTS(48 bits)  |      
				*/
				payload_size = (sizeof(sMPEG2_TS_PAYLOAD) - 14);  
			}else{	
				/* others not the payload unit start pkt, all the rest are video data no PES HDR*/
				if(afc == 0x01){
					/* pure video payload */
					payload_size = sizeof(sMPEG2_TS_PAYLOAD);
				}else if(afc == 0x03){
					/* adaptation field + video payload */
					payload_size = sizeof(sMPEG2_TS_PAYLOAD) - 1 - ts->payload.payload[0];   ///< ignore adaptation field length and itseslf
				}else{
					assert(0);
				}	
					
			}
			pes_byte_count += payload_size;
		}
		bytes_left -= sizeof(sMPEG2_TS);
		curr_ptr   += sizeof(sMPEG2_TS);
	
	} while(bytes_left > 0);

	/* Record the last MPEG2-TS pkt Continuity counter value, and temporal total length of PES */
	*cc_end 	  = cc;
	*pes_payload_size = pes_byte_count;
}

/* -------------------------------------------------
/       slice_pkt_add() 
/		Judge 2 situations(empty/already one slice packet),and connect linklist,and update head/tail pointer address              
/---------------------------------------------------*/
static void slice_pkt_add(
	sSX_DESC	*desc
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
/		Judge 2 situations(empty/already one slice packet),and connect linklist,and update head/tail pointer address              
/---------------------------------------------------*/
static void slice_drop(void){
	
	sSX_DESC	*curr;
	sSX_DESC	*next;

	/* Drop slice ,Release desc address from head to tail */
	sx_desc_put(f_cblk.slice_chain.head);
	
	/* Reset head and tail */
	f_cblk.slice_chain.head = NULL;
	f_cblk.slice_chain.tail = NULL;

	printf("slice dropped() invoked\n");
}

/* -------------------------------------------------
/       video_decoder_thread() 
/ 		First, find the slice-start and record PTS.
/		Second, if find the slice-start try to organize the slice chain,and check MPEG2-TS Continous Counter to know whether loss pkt
/			if loss pkt,and then slice drop and re-organize the slice chain
/		Third,  if find the other slice-start,means that old slice chain is complete, [dump] the old slice chain, and re-organize the new slice chain
/---------------------------------------------------*/
static void video_decoder_thread(void *arg){
	
	sSX_DESC	*desc;
	sSX_DESC	*slice_start;
	sSX_DESC	*slice_end;
	UINT8		*curr_ptr;
	UINT32		bytes_left;
	sMPEG2_TS	*ts;
	UINT32		pes_payload_len;
	UINT8		slice_start_found;
	UINT8		cc_start;
	UINT8		cc_end;
	UINT32		pes_payload_size;

	while(1){
		
		do{
		
			desc = sx_pipe_get(SX_VRDMA_VIDEO_PKT_QUEUE);	///< dequeue
			if(desc == NULL){
				break;
			}		
			
			/*Get current pkt data */
	  		curr_ptr = desc->data;
		
			/* Get packet length */
			bytes_left = desc->data_len;
			assert(bytes_left > sizeof(sRTP_HDR));
		
			/* TODO GET RTP Header, need this line to record rtp_hdr struct data*/
			sRTP_HDR *rtp_hdr = (sRTP_HDR *) curr_ptr;

			/* GET MPEG2-TS Header */
			curr_ptr += sizeof(sRTP_HDR);   ///< point to the RTP payload initial address
			ts = (sMPEG2_TS *) curr_ptr;

			/* Always look for slice start */
			UINT64	pts_ms;	  ///< presentation timestamp
			slice_start_found = slice_start_find(desc, &pes_payload_len, &pts_ms);	
			
			/* check whether find the new slice */
			if(f_cblk.look_for_new_slice){
				if(!slice_start_found){
					
					/* Looking for a new slice but didn't find a slice start */
					/* Retry with the next pkt */
					goto cleanup;
				}

				/* Found new slice. Keep going */
				f_cblk.look_for_new_slice = 0;
			}
	
			/* Check whether find the slice-start, it will happen 3 situations:
			   first: 
	 			  non-slice-start,ignore it
				  <=> find slice start & it's first slice	 
				      record its PTS, know when to render this slice
		 	   second:
				  <=> TODO would this situation happen?	
				  |non-slice-start|non-slice-start|...|non-slice-start|slice-start|
			   third:
 				  <=> slice_pkt_count > 0, indicate fina a new slice,so dump the old slice chain,and reset parameters
				  |old slice-start|non-slice-start|...|non-slice-start|new slice-start|

			*/
			if(slice_start_found){
				
				/* Check whether there is the first slice */
				if(f_cblk.slice_count == 0){
					f_cblk.slice_pts_ms	= pts_ms;	
				}
				
				
				if(f_cblk.slice_pkt_count > 0){
				
					/* If we already have a chain and found a slice start (complete slice), dump the slice  */
					/* [Dump] means that Orginize a (SLICE HDR + SLICE CHAIN) ,and push into SLICE QUEUE */
					video_decoder_slice_dump(f_cblk.slice_pts_ms, slice_chain_get());
	
					/* Reset slice packet count , to re-organize a SLICE CHAIN */
					f_cblk.slice_pkt_count		= 0;	///< count slice chain link-list
					f_cblk.pes_payload_curr_len	= 0;
					f_cblk.slice_pts_ms		= pts_ms;	/* Get this from slice_start_find()
											   Indicate to find a new slice chain   	
											*/	
					
				}
				
				f_cblk.slice_count++;	///< count the slice
			}

			/* Interpret packet(get temporal total length of PES,and MPEG2-TS continuous counter start/end value) */
			m2ts_data_interpret(desc, &pes_payload_size, &cc_start, &cc_end);
		
			f_cblk.pes_payload_curr_len += pes_payload_size;   ///< TODO not realy used f_cblk.pes_payload_curr_len

			/* Below is to organize the Slice chain(link-list) ,but first to check whether MPEG2-TS pkt is continuous. 
			   If it's continuous, slice chian will be complete.
			   If it's not, slice chain will be drop.Get the next new slice. 
			*/
			if(f_cblk.slice_pkt_count ==0){
				
				/* If first packet, just cache the last CC */
				f_cblk.last_cc = cc_end;
			}else{
				/* If not first packet, check for continuity */
				if(((f_cblk.last_cc + 1) & 0x0F) == cc_start){
					/* Passes continuity test */
					f_cblk.last_cc = cc_end;
				}else{
					/* Fails continuity test */
					printf("(video_decoder): curr rtp seq num = %d\n", ntohs(rtp_hdr->sequence_num));
					printf("(video_decoder): Packet loss detected! last_cc = %u, start_cc = %d\n", f_cblk.last_cc, cc_start);

					slice_drop();
					f_cblk.look_for_new_slice = 1;
					f_cblk.slice_pkt_count = 0;
					
					goto cleanup;

				}	
			}
			
			/* Originize the Slice chain(link-list) ,add slice into slice chain,so plus the slice_pkt */
			slice_pkt_add(desc);

			f_cblk.slice_pkt_count ++;

	cleanup:
		printf("(video_decoder) slice broken ,try again\n");		

		} while(1);

		usleep(8 * 1000);
	}	
}

/* -------------------------------------------------
/       sx_mgmt_video_decoder_open() 
/              Create a pthread to decode video pkt
/---------------------------------------------------*/
void sx_mgmt_video_decoder_open(void){
	/* Create a pthread to decode video pkt */
	sx_thread_create(&f_cblk.decoder_thread, &video_decoder_thread, NULL, VIDEO_DECODER_THREAD_PRIORITY);
}

/* -------------------------------------------------
/       sx_mgmt_video_decoder_init 
/              Init video decoder resource
/---------------------------------------------------*/
void sx_mgmt_video_decoder_init(void){
	
	/* Initialize common resources */
	f_cblk.look_for_new_slice = 1;
}	
