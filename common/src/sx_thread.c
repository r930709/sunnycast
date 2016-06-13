#include <string.h>
#include <pthread.h>

/* -------------------------------------------------
/       sx_thread_create   
/               pthread create, set thread priority 
/---------------------------------------------------*/
void sx_thread_create(
	void		*thread_id,
	void		*thread_func,
	void		*thread_arg,
	unsigned int	thread_priority
){
	struct sched_param	param;

	// Create thread
	pthread_create(thread_id, NULL, (void *)thread_func, NULL);

	// Set thread priority,Launch the program using sudo to set the thread priority
	memset(&param, 0, sizeof(param));
	param.sched_priority = thread_priority;
	pthread_setschedparam(*(int *) thread_id, SCHED_FIFO, &param);
}


/* -------------------------------------------------
/       sx_thread_join   
/               pthread join 
/---------------------------------------------------*/
void sx_thread_join(
	void 		*thread_id,
	void		**retval
){
	pthread_join((pthread_t)thread_id, retval);
	
}
