#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <netinet/in.h>

#include "sx_types.h"
#include "sx_thread.h"
#include "sx_udp.h"
#include "sx_pkt.h"
#include "sx_desc.h"
#include "sx_pipe.h"

#define WFD_UDP_PORT	50000

/* -------------------------------------------------
/       sMGMT_DATA_CBLK 
/              the object of sMGMT_DATA_control_block which owns pkt_rx_pthread,UDP fd,rx_pkt_len
/---------------------------------------------------*/  
typedef struct{
	pthread_t	pkt_rx_thread;	//< Packet receive thread id >
	S_UDP_ID	udp_sock;	//< UDP Socket fd  >
	UINT32		rx_pkt_len;		//< Number of received packet>
} sMGMT_DATA_CBLK;

static sMGMT_DATA_CBLK		f_cblk;

/* -------------------------------------------------
/       sx_mgmt_data_init 
/              UDP socket init,prepare to receive multimedia stream from wfd source
/---------------------------------------------------*/  
void sx_mgmt_data_init(void){
		
	f_cblk.udp_sock = sx_udp_create(WFD_UDP_PORT);		
	printf("(mgmt_data): mgmt_data_init(): Initialized.\n");
}

/* -------------------------------------------------
/       pkt_rv_thread() 
/              pthread to receive UDP socket packet,
/---------------------------------------------------*/  
static void pkt_rv_thread(void *arg){
	
	sPI_PORTAL_PKT	*pkt;   ///< hdr+payload(1468bytes)
	UINT16		last_seq_num;
	UINT16		curr_seq_num;

	while(1){
		
		/* Malloc enough packet size to hold data  */
		pkt = malloc(sizeof(sPI_PORTAL_PKT));
		
		/* Init receive packet len*/
		UINT32	pkt_len = sizeof(sPI_PORTAL_PKT);
	
		/* Wait for packet recv */
		sx_udp_recv(f_cblk.udp_sock, (char *) pkt, &pkt_len);

		/* check whether if missing packets */
		sRTP_HDR *rtp_hdr = (sRTP_HDR *) pkt;

		curr_seq_num = ntohs(rtp_hdr->sequence_num);	///< point means shift next how many bytes from initial address 
		if((UINT16) (last_seq_num + 1) != curr_seq_num){

			printf("(mgmt): last_seq_num = %u, curr_seq_num = %u, lost %u pkts\n",last_seq_num,
											      curr_seq_num,
											      curr_seq_num - (last_seq_num + 1));
		}

		/* cache last sequence number */
		last_seq_num = curr_seq_num;

		/* push packet to process queue */
		sSX_DESC *desc = sx_desc_get();
		
		desc->data	= (void *) pkt;
		desc->data_len 	= pkt_len;
		
		sx_pipe_put(SX_VRDMA_PKT_QUEUE, desc);
	
		f_cblk.rx_pkt_len++;

	}

}


/* -------------------------------------------------
/       sx_mgmt_data_open 
/              Create a pthread to receive UDP socket data
/---------------------------------------------------*/  
void sx_mgmt_data_open(void){
	
	sx_thread_create(&f_cblk.pkt_rx_thread, &pkt_rv_thread, NULL, MGMT_DATA_RX_THREAD_PRIORITY);
	printf("(mgmt_data_open): pthread create,Open\n");
}
