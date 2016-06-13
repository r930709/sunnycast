#if !defined(_SX_DESC_H_)
#define _SX_DESX_H_

#include "sx_types.h"

/* -------------------------------------------------
/       sSX_DESC 
/              Descriptor contain data which will point to packet,data_len will equcal packet length, next point to the next sSX_DESC struct node
/---------------------------------------------------*/
/*TODO wht warrning use typedef struct{} sSX_DESC;*/
typedef struct sSX_DESC{
	UINT8			*data;
	UINT8			data_len;
	struct sSX_DESC		*next;
} sSX_DESC;


/* -------------------------------------------------
/       sx_desc_get 
/              Get a Descriptor and init
/---------------------------------------------------*/
extern sSX_DESC * sx_desc_get(void);


/* -------------------------------------------------
/       sx_desc_put 
/              Free a Descriptor or a Descriptor chain
/---------------------------------------------------*/
extern void sx_desc_put(
	
	sSX_DESC	*desc
);

#endif
