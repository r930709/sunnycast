#if !defined(_SX_QUEUE_H_)
#define _SX_QUEUE_H_


#define SX_QUEUE	void *

extern SX_QUEUE sx_queue_create(void);

extern void sx_queue_push(
	SX_QUEUE	queue_id,	//queue index
	void		*data
);

extern SX_QUEUE sx_queue_pull(
	SX_QUEUE	queue_id	//queue index
);

extern void sx_queue_destory(
	SX_QUEUE	queue_id
);

extern unsigned int sx_queue_len_get(
	SX_QUEUE	queue_id
);

#endif
