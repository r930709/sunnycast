#if !defined(AUDIO_SINK_H)
#define AUDIO_SINK_H

#include "sx_types.h"

extern void sx_audio_init(void);

extern UINT8 *sx_audio_sink_buffer_get(void);

extern void sx_audio_sink_buffer_set(
	UINT8	*buf,
	UINT32	buf_len
);

extern UINT32 sx_audio_sink_ms_left_get(void);


/* Contorl audio playback speed */
extern void sx_audio_playback_speed_inc(void);

extern void sx_audio_playback_speed_reset(void);

extern void sx_audio_playback_speed_dec(void);


#endif
