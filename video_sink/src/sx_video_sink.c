#include <stdio.h>
#include <assert.h>

#include "bcm_host.h"
#include "ilclient.h"

/* TODO */
//#include "sx_mgmt_env.h"
#include "sx_types.h"
#include "sx_video_sink.h"


/* -------------------------------------------------
/       OpenMax object 
/              Component,tunnel,ilclient
/---------------------------------------------------*/
static COMPONENT_T	*video_decode;
static COMPONENT_T	*video_scheduler;
static COMPONENT_T	*video_render;
static COMPONENT_T	*video_clock;
static COMPONENT_T	*comp_list[5];
static TUNNEL_T		tunnel[4];
static ILCLIENT_T	*client;

static UINT32		port_setting_changed = 0;
static UINT32		first_packet = 1;


/* -------------------------------------------------
/       sx_video_sink_init() 
/              
/---------------------------------------------------*/
void sx_video_sink_init(void){
	
	OMX_VIDEO_PARAM_PORTFORMATTYPE	format;
	OMX_TIME_CONFIG_CLOCKSTATETYPE	cstate;
	int rc;

	/* Init compoent & tunnel */
	memset(comp_list, 0, sizeof(comp_list));
	memset(tunnel, 0, sizeof(tunnel));

	/* Init Broadcom host */
	bcm_host_init();

	/* Init il client */
	client = ilclient_init();
	assert(client != NULL);

	/* Init OpenMax */
	rc = OMX_Init();
	assert(rc == OMX_ErrorNone);

	/* Create Decoder Component,beacuse enable input buffers,so in the end need to OMX_EmptyThisBuffer */
	rc = ilclient_create_component( client,
					&video_decode,
					"video_decode",
					ILCLIENT_DISABLE_ALL_PORTS | ILCLIENT_ENABLE_INPUT_BUFFERS);
	assert(rc == 0);
	comp_list[0] = video_decode;

	/* Create video render */
	rc = ilclient_create_component( client,
					&video_render,
					"video_render",
					ILCLIENT_DISABLE_ALL_PORTS);
	assert(rc == 0);
	comp_list[1] = video_render;

	/* Create clock */
	rc = ilclient_create_component( client,
					&video_clock,
					"clock",
					ILCLIENT_DISABLE_ALL_PORTS);
	assert(rc == 0);
	comp_list[2] = video_clock;

	/* Configure clock */
	memset(&cstate, 0, sizeof(cstate));
	cstate.nSize			= sizeof(cstate);
	cstate.nVersion.nVersion	= OMX_VERSION;
	cstate.eState			= OMX_TIME_ClockStateWaitingForStartTime;
	cstate.nWaitMask		= 1;
	
	rc = OMX_SetParameter(ILC_GET_HANDLE(video_clock),
			      OMX_IndexConfigTimeClockState,
			      &cstate);
	assert(rc == 0);

	/* Create video scheduler */
	rc = ilclient_create_component( client,
					&video_scheduler,
					"video_scheduler",
					ILCLIENT_DISABLE_ALL_PORTS);
	assert(rc == 0);
	comp_list[3] = video_scheduler;

	/* Set tunnels  */
	/* Connect decode to scheduler */
	set_tunnel(tunnel, video_decode, 131, video_scheduler, 10);
	
	/* Connect scheduler to render */
	set_tunnel(tunnel+1, video_scheduler, 11, video_render, 90);

	/* Connect clock to scheduler */
	set_tunnel(tunnel+2, video_clock, 80, video_scheduler, 12);

	/* Setup clock tunnel first */
	rc = ilclient_setup_tunnel(tunnel+2, 0, 0);
	assert(rc == 0);

	/* Kick start the clock */
	ilclient_change_component_state(video_clock, OMX_StateExecuting);

#define AUTO_FULLSCREEN

#if 1
	OMX_CONFIG_DISPLAYREGIONTYPE	drt;
	memset(&drt, 0, sizeof(drt));
	drt.nVersion.nVersion		= OMX_VERSION;
	drt.nSize			= sizeof(drt);
	drt.nPortIndex			= 90;
/* if not defined AUTO_FULLSCREEN it means that set other display region type */	
#if 0
#if !defined(AUTO_FULLSCREEN)
	/*TODO*/
	drt.src_rect.x_offset 		= 0;
	drt.src_rect.y_offset		= 0;
	drt.src_rect.width		= sx_mgmt_env_get(MGMT_ENV_VAR_SESSION_WIDTH);
	drt.src_rect.height		= sx_mgmt_env_get(MGMT_ENV_VAR_SESSION_HEIGHT);
	drt.dest_rect.x_offset		= -56;
	drt.dest_rect.y_offset		= 0;
	drt.dest_rect.width		= 1792;
	drt.dest_rect.height		= 1050;
#endif
#endif

#if !defined(AUTO_FULLSCREEN)
	drt.fullscreen			= OMX_FALSE;
#else
	/* define AUTO_FULLSCREEN */
	drt.fullscreen			= OMX_TRUE;
#endif
	
	drt.noaspect			= OMX_TRUE;
	drt.mode			= OMX_DISPLAY_MODE_FILL;

#if !defined(AUTO_FULLSCREEN)
	/*TODO*/
	drt.set = (OMX_DISPLAYSETTYPE) ( OMX_DISPLAY_SET_SRC_RECT | 
					 OMX_DISPLAY_SET_DEST_RECT |
					 OMX_DISPLAY_SET_FULLSCREEN |
					 OMX_DISPLAY_SET_NOASPECT);
#else
	drt.set = (OMX_DISPLAYSETTYPE) (OMX_DISPLAY_SET_FULLSCREEN |
					OMX_DISPLAY_SET_NOASPECT);
#endif

	rc = OMX_SetConfig(ILC_GET_HANDLE(video_render), OMX_IndexConfigDisplayRegion, &drt);
	assert(rc==0);
#endif
	
	/* Kick start video decoder */
	ilclient_change_component_state(video_decode, OMX_StateIdle);
	
	/*TODO Configure decoder */
	memset(&format, 0, sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE));
	format.nSize			= sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE);
	format.nVersion.nVersion	= OMX_VERSION;
	format.nPortIndex		= 130;
	format.eCompressionFormat	= OMX_VIDEO_CodingAVC;

	rc = OMX_SetParameter(ILC_GET_HANDLE(video_decode),
			      OMX_IndexParamVideoPortFormat,
			      &format);	
	assert(rc == 0);

	/* Enable video decode  */
	rc = ilclient_enable_port_buffers(video_decode, 130, NULL, NULL, NULL);
	assert(rc ==0);
	
	/* Kick start video decode  */
	ilclient_change_component_state(video_decode, OMX_StateExecuting);

	printf("decoder_hw: initialized");

}

/* -------------------------------------------------
/       sx_video_sink_buf_get() 
/		Get a hw buffer overlay header
/---------------------------------------------------*/
sDECODER_HW_BUFFER * sx_video_sink_buf_get(void){
	OMX_BUFFERHEADERTYPE	*buf;

	/* Attempt tp get a hw buffer */
	buf = ilclient_get_input_buffer(video_decode, 130, 0);

	/* return our overlay hw_buffer_struct */
	return (sDECODER_HW_BUFFER *) buf;

}

/* -------------------------------------------------
/       sx_video_sink_buf_set() 
/              
/---------------------------------------------------*/
void sx_video_sink_buf_set(
	sDECODER_HW_BUFFER	*decoder_hw_buf
){
	OMX_BUFFERHEADERTYPE	*buf;
	int			rc;

	buf = (OMX_BUFFERHEADERTYPE *) decoder_hw_buf;
	
	if(!port_setting_changed && 
	    (ilclient_remove_event(video_decode, OMX_EventPortSettingsChanged, 131, 0, 0, 1) == 0)){
	
		printf("(decoder_hw): port setting changed!\n");

		port_setting_changed = 1;

		/* Setup tunnel for ... */
		rc = ilclient_setup_tunnel(tunnel, 0 ,0);
		assert(rc = 0);
	
		/* Kick start scheduler */
		ilclient_change_component_state(video_scheduler, OMX_StateExecuting);
		
		rc = ilclient_setup_tunnel(tunnel+1, 0, 1000);	///< TODO 1000?
		assert(rc == 0);

		/* Kick start render */
		ilclient_change_component_state(video_render, OMX_StateExecuting);
	}

	/* Set buffer property */
	buf->nOffset = 0;
	buf->nFlags  = first_packet ? OMX_BUFFERFLAG_STARTTIME : OMX_BUFFERFLAG_TIME_UNKNOWN;
	first_packet = 0;

	/* Flush into the decoder */
	rc = OMX_EmptyThisBuffer(ILC_GET_HANDLE(video_decode), buf);
	assert(rc == 0);
	
	
}
