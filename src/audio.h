#pragma once

#include <Limelight.h>

void audio_init_callbacks(AUDIO_RENDERER_CALLBACKS *cb);
#ifdef _XBOX
int audio_select_configuration(bool surround);
const char *audio_fallback_reason(void);
void audio_set_volume(int volume);
int audio_output_channels(void);
void audio_menu_configure(bool enabled,int volume);
void audio_menu_play(int effect); /* 0 navigation, 1 confirm, 2 back, 3 error */
void audio_menu_shutdown(void);
#endif
