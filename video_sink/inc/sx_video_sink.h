#if !defined(DECODER_HW_H)
#define DECODER_HW_H

#include "sx_types.h"

/*TODO*/
#define DECODER_HW_BUFFER_SIZE_MAX	(16 * 1024)

/* This is an overlay of OMX_BUFFERHEADERTYPE to reduce dependency */
typedef struct{
	
	UINT32		reserved1;	///< nSize
	UINT32		reserved2;	///< nVersion
	UINT8		*buffer;	///< pBuffer	
	UINT32		reserved3;	///< nAllocLen
	UINT32		buffer_len;	///< nFilledLen
	UINT32		reserved4;	///< nOffset
	UINT32		reserved5;	///< pAppPrivate		
	UINT32		reserved6;	///< pPlatformPrivate
	UINT32		reserved7;	///< pInputPorPrivate	
	UINT32		reserved8;	///< pOutputPortPrivate
	UINT32		reserved9;	///< hMarkTargetComponent	
	UINT32		reserved10;	///< pMarkData	
	UINT32		reserved11;	///< nTickCount
	UINT32		reserved12;	///< nTimeStamp	
	UINT32		reserved13;	///< nFlags
	UINT32		reserved14;	///< nOutputPortIndex
	UINT32		reserved15;	///< nInputPortIndex	

} sDECODER_HW_BUFFER;

extern void sx_video_sink_init(void);

/* Get decoder hardware buffer(it's like get this object) */
extern sDECODER_HW_BUFFER * sx_video_sink_buf_get(void);

/* Set decoder hardware buffer content */
extern void sx_video_sink_buf_set(

	sDECODER_HW_BUFFER	*decoder_hw_buf
);

#endif
