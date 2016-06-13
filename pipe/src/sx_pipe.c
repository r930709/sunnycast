#include "sx_types.h"
#include "sx_queue.h"
#include "sx_pipe.h"


/* -------------------------------------------------
/       sVRDMA_CBLK 
/              the object of sVRDMA_control_block which owns 7 queue recorders
/---------------------------------------------------*/
typedef struct{

	SX_QUEUE	queue[SX_VRDMA_MAX];	
} sVRDMA_CBLK;

static sVRDMA_CBLK	f_cblk;

/* -------------------------------------------------
/       sx_pipe_init
/              Create 7 queue recorders,each will contaion resource which be processed
/---------------------------------------------------*/
void sx_pipe_init(void){
	
	UINT32	i;
	
	for(i=0 ; i < SX_VRDMA_MAX ; i++){
		f_cblk.queue[i] = sx_queue_create();
	}
}

/* -------------------------------------------------
/       sx_pipe_put()
/              enqueue and push resource,update queue recorder information
/---------------------------------------------------*/
void sx_pipe_put(
	UINT32		index,
	void		*data
){
	sx_queue_push(f_cblk.queue[index], data);
}


/* -------------------------------------------------
/       sx_pipe_get()
/              dequeue and pull resource,update queue recorder information
/---------------------------------------------------*/
void *sx_pipe_get(
	UINT32		index
){
	return sx_queue_pull(f_cblk.queue[index]);
}	

/* -------------------------------------------------
/       sx_pipe_len_get()
/              pass the queue[index], and then get the queue->len
/---------------------------------------------------*/
UINT32 sx_pipe_len_get(
	UINT32		index
){
	return sx_queue_len_get(f_cblk.queue[index]);
}
