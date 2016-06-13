#if !defined(_UDP_SOCKET_H_)
#define _UDP_SOCKET_H_

#include <sx_types.h>

#define S_UDP_ID	void *

extern S_UDP_ID	sx_udp_create(
	UINT16		local_port
);

extern void sx_udp_recv(
	S_UDP_ID	id,
	UINT8		*pkt,
	UINT32		*pkt_len
);

#endif
