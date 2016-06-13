#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
//#include <semaphore.h>
#include <assert.h>

#include "sx_types.h"
#include "sx_queue.h"

/* TODO why typedef struct {}sNODE;  */
typedef struct sNODE
{
	struct sNODE	*next;
	void		*data;	
} sNODE;

typedef struct
{
	sNODE		*head;
	sNODE		*tail;
	pthread_mutex_t	 lock;
	//sem_t		 sem;
	UINT32		 len;
} sQUEUE;

/* -------------------------------------------------
/       sx_queue_create() 
/              Init a queue recorder to record queue information,return queue recorder
/---------------------------------------------------*/
SX_QUEUE sx_queue_create(void){
	
	sQUEUE	*queue = malloc(sizeof(sQUEUE));
	
	pthread_mutex_init(&queue->lock, NULL);
	
	queue->head	= NULL;
	queue->tail	= NULL;
	queue->len	= 0;

	return queue;
}


/* -------------------------------------------------
/       sx_queue_psuh() 
/		enqueue,and queue recorder update head or tail address  
/---------------------------------------------------*/
void sx_queue_push(
	SX_QUEUE	queue_id,
	void		*data
){
	sNODE	*node;
	sQUEUE	*queue;
	
	/* Debug : check whether data is NULL */
	assert(data != NULL);

	/* Get queue recorder id */
	queue = queue_id;

	/* Create a new node */
	node = malloc(sizeof(sNODE));
	node->data = data;
	node->next = NULL;

	/* Get lock */
	pthread_mutex_lock(&queue->lock);

	/* Check whether queue is empty */
	if(queue->head == NULL){
		
		/* queue is empty, update head and tail address of queue recorder */
		queue->head = node;
		queue->tail = node;
	
		goto finish_enqueue;
	}
	
	/* queue is not empty ,change tail->next and  update tail address of queue recorder*/
	queue->tail->next = node;
	queue->tail = node;	

finish_enqueue:
	
	queue->len++;
	
	/* Release the lock */
	pthread_mutex_unlock(&queue->lock);	
}


/* -------------------------------------------------
/       sx_queue_pull()
/		dequeue,and queue recorder update head or tail address  
/---------------------------------------------------*/
SX_QUEUE sx_queue_pull(
	SX_QUEUE	queue_id
){
	
	sQUEUE		*queue;
	sNODE		*tmp_head;
	UINT8		*data;
	
	
	/* Get queue recorder id */
	queue = queue_id;

	/* Get lock */
	pthread_mutex_lock(&queue->lock);


	/* Check whether queue is empty */
	if(queue->head == NULL){
				
		assert(queue->tail == NULL);
		
		/* Return queue is empty */
		data = NULL;
		
		goto finish_dequeue;	
	}

	/* Check whether there is only one node in this queue */
	if(queue->head == queue->tail){

		/* Debug: only one node,there is no more next node in this queue */
		assert(queue->head->next == NULL);
		assert(queue->tail->next == NULL);
	
		/* Get only one node data */
		data = queue->head->data;
		
		/* free only one node,update head and tail address of queue recorder */
		free(queue->head);
		queue->head = NULL;
		queue->tail = NULL;

		goto finish_dequeue;			
			
	}	

	/* There is more than one node in this queue */
	data = queue->head->data;
	
	/* update head address of queue recorder and free the node which dequeued */
	tmp_head = queue->head->next;
	free(queue->head);
	queue->head = tmp_head;

finish_dequeue:
	
	if(data!=NULL){
		queue->len--;
	}	
	
	pthread_mutex_unlock(&queue->lock);
	
	return data;
}

void sx_queue_destroy(
	SX_QUEUE	queue_id
){
	/* TODO: */
}

/* -------------------------------------------------
/       sx_queue_len_get()
/		Use pthread mutex to prevent synchronous,ant then get queue->len  
/---------------------------------------------------*/
UINT32 sx_queue_len_get(
	SX_QUEUE	queue_id
){
	sQUEUE		*queue;
	UINT32		len;

	/* Get queue id */
	queue = queue_id;

	pthread_mutex_lock(&queue->lock);

	len = queue->len;

	pthread_mutex_unlock(&queue->lock);

	return (len);
}
