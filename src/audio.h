#pragma once

#include <Limelight.h>

void audio_init_callbacks(AUDIO_RENDERER_CALLBACKS *cb);
#ifdef _XBOX
int audio_select_configuration(bool surround);
const char *audio_fallback_reason(void);
void audio_set_volume(int volume);
int audio_output_channels(void);
#endif
