#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

#include "sx_thread_priority.h"
#include "sx_thread.h"
#include "sx_pipe.h"
#include "sx_mgmt_data.h"
#include "sx_mgmt_m2ts_decoder.h"
#include "sx_mgmt_video_decoder.h"
#include "sx_mgmt_video_scheduler.h"
#include "sx_mgmt_audio_decoder.h"
#include "sx_mgmt_audio_scheduler.h"


static pthread_t 	f_mgmt_sys_thread;

static void mgmt_sys_thread(void){
	do{
		usleep(1*1000*1000);
	}while(1);
}

/* -------------------------------------------------
/	mgmt_sys_init	
/		init pipeline, UDP packet manager, each Openmax component 
/---------------------------------------------------*/
void mgmt_sys_init(void){
	
	// Initialize pipeline
	sx_pipe_init();

	// Initialize UDP packets manager
	sx_mgmt_data_init();

	// Initialize m2ts decoder
	sx_mgmt_m2ts_decoder_init();

	// Initialize video decoder component
	sx_mgmt_video_decoder_init();

	// Initialize video scheduler component
	sx_mgmt_video_scheduler_init();

	// Initialize audio decoder component
	sx_mgmt_audio_decoder_init();

	// Initialize audio scheduler component
	sx_mgmt_audio_scheduler_init();

	printf("(mgmt_sys): (mgmt_sys_init): Done.\n");
} 


/* -------------------------------------------------
/	mgmt_sys_open	
/		start pipeline, UDP socket manager, each Openmax component 
/---------------------------------------------------*/
void mgmt_sys_open(void){
	
	
	// Start UDP packets manager
	sx_mgmt_data_open();

	// Start m2ts decoder
	sx_mgmt_m2ts_decoder_open();

	// Start video decoder component
	sx_mgmt_video_decoder_open();

	// Start video scheduler component
	sx_mgmt_video_scheduler_open();

	// Start audio decoder component
	sx_mgmt_audio_decoder_open();

	// Start  audio scheduler component
	sx_mgmt_audio_scheduler_open();

	printf("(mgmt_sys): (mgmt_sys_open): Done.\n");

	// mgmt_sys_thread create,wait forever
	sx_thread_create(&f_mgmt_sys_thread, &mgmt_sys_thread, NULL, MGMT_SYS_THREAD_PRIORITY);
	sx_thread_join(&f_mgmt_sys_thread,NULL);
			
}
