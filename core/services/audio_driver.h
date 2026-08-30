#ifndef AUDIO_DRIVER_H
#define AUDIO_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the audio playback engine
int audio_init(void);

void audio_play_sound(const char* filepath);

void audio_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif // AUDIO_DRIVER_H