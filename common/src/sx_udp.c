#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>

#include "sx_types.h"
#include "sx_udp.h"

/* -------------------------------------------------
/       sUDP_CBLK 
/              the object of sUDP_control_block which owns udp socket fd
/---------------------------------------------------*/
typedef struct{
	
	int			sock;
	struct sockaddr_in	*client_addr;	
} sUDP_CBLK;

/* -------------------------------------------------
/       sx_udp_create 
/              udp socket init
/---------------------------------------------------*/
S_UDP_ID sx_udp_create(
	UINT16		port
){
	
	struct sockaddr_in	tmp_client_addr;

	sUDP_CBLK *udp_cblk = malloc(sizeof(sUDP_CBLK));
	
	memset(udp_cblk, 0,sizeof(sUDP_CBLK));
	
	/* Create udp socket */
	udp_cblk->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);	
	if( (udp_cblk->sock) < 0){
		perror("Opening datagram socket error");
		exit(1);
	}

	/* Listen to anyone that talks to this sink device on the right port */
	tmp_client_addr.sin_family	= AF_INET;
	tmp_client_addr.sin_port	= htons(port);
	tmp_client_addr.sin_addr.s_addr	= INADDR_ANY;
	bzero(&(tmp_client_addr.sin_zero), 8);
  
	if(bind(udp_cblk->sock, 
		(struct sockaddr *) &tmp_client_addr, 
		sizeof(tmp_client_addr))){
		
		perror("socket bind error");
		close(udp_cblk->sock);
		exit(1);
	}  
	
	udp_cblk->client_addr = &tmp_client_addr;
			
	return udp_cblk;
}

/* -------------------------------------------------
/       sx_udp_recv
/              sink device receive multimedia stream from wfd source
/---------------------------------------------------*/
void sx_udp_recv(
	S_UDP_ID	id,
	UINT8		*pkt,
	UINT32		*pkt_len
){
	UINT32			bytes_read;
	sUDP_CBLK		*udp_cblk = id;
	struct sockaddr_in	tmp_client_addr = *(udp_cblk->client_addr);
	UINT32			addr_len;

	addr_len = sizeof(struct sockaddr);	

			
	if(bytes_read = recvfrom(udp_cblk->sock, 
				 pkt, 
				 *pkt_len,
				  0, 
				  (struct sockaddr *) &tmp_client_addr, 
				  &addr_len) < 0)
	{
		perror("Receiving datagram message error");
		exit(1);
	}else{
		printf("Receiving datagram message... %d bytes OK\n",bytes_read);
	}
	
	*pkt_len = bytes_read;           	
}

