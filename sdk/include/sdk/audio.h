#ifndef SDK_AUDIO_H
#define SDK_AUDIO_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { SDK_AUDIO_MAX_CHANNELS = 8, SDK_AUDIO_BLOCK_FRAMES = 256 };
/* Return codes: 0 success/empty, 1 packet received, negative = error. */
enum {
	SDK_MEDIA_INVALID = -1,
	SDK_MEDIA_UNAVAILABLE = -2,
	SDK_MEDIA_FULL = -3,
	SDK_MEDIA_BUSY = -4
};
typedef struct {
	uint32_t generation, sample_rate_hz, device_block_frames;
	unsigned input_channels, output_channels;
	int running;
} sdk_audio_format_t;
typedef struct {
	uint32_t generation, sample_rate_hz;
	unsigned channels, frames;
	uint64_t timestamp_us;
	/* Interleaved float PCM, nominally [-1, 1]. Only frames * channels used. */
	float samples[SDK_AUDIO_BLOCK_FRAMES * SDK_AUDIO_MAX_CHANNELS];
} sdk_audio_packet_t;
typedef struct {
	const float *const *input;
	float *const *output;
	unsigned input_channels, output_channels, frames;
	uint32_t sample_rate_hz;
} sdk_audio_process_block_t;
typedef struct {
	void (*process)(const sdk_audio_process_block_t *block, void *context);
	void *context;
} sdk_audio_processor_t;
typedef struct {
	uint64_t callbacks, input_dropped_blocks, output_underrun_frames;
} sdk_audio_stats_t;
/* Register an immutable binding. Binding/context must remain valid until HOST
 * audio shutdown, even after replacement. Callback runs in HOST realtime context:
 * no allocation, waits, locks, file I/O, LVGL or FreeRTOS calls. Outputs start at
 * silence. A registered processor owns output; queued playback is then rejected. */
void sdk_audio_set_processor(const sdk_audio_processor_t *processor);
/* Capture is opt-in. Reading is nonblocking; disabling leaves queued data to drain. */
void sdk_audio_capture_enable(int enabled);
/* Atomic output gate for the RTOS engine. Muted callbacks drain queued output
 * and emit silence. Does not stop the device or affect capture. */
void sdk_audio_playback_mute(int muted);
/* Request a bounded callback-side discard of pending queued playback. */
void sdk_audio_playback_clear(void);
int sdk_audio_get_format(sdk_audio_format_t *format);
sdk_audio_stats_t sdk_audio_stats(void);
int sdk_audio_read(sdk_audio_packet_t *packet);
/* Submit using the current generation/rate/channel count from get_format().
 * Full queues reject the new packet. Device changes discard stale output. */
int sdk_audio_write(const sdk_audio_packet_t *packet);
#ifdef __cplusplus
}
#endif
#endif
