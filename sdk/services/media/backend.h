#pragma once
#include "sdk/audio.h"
#include "sdk/midi.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Platform adapter only. Audio lifecycle/process calls must be serialized by
 * the backend. No JUCE, Windows or RTOS types cross this boundary. */
void sdk_media_audio_start(unsigned rate, unsigned inputs, unsigned outputs,
			   unsigned frames);
void sdk_media_audio_stop(void);
void sdk_media_audio_process(const float *const *input, unsigned inputs,
			     float *const *output, unsigned outputs,
			     unsigned frames, uint64_t timestamp_us);
void sdk_media_midi_devices(unsigned inputs, unsigned outputs, unsigned enabled,
			    int available);
void sdk_media_midi_input(uint32_t port, const uint8_t *data, unsigned size,
			  uint64_t timestamp_us);
int sdk_media_midi_output(sdk_midi_message_t *message);
void sdk_media_midi_delivered(int success);
#ifdef __cplusplus
}
#endif
