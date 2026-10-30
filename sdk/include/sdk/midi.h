#ifndef SDK_MIDI_H
#define SDK_MIDI_H
#include <stdint.h>
#include "sdk/audio.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { SDK_MIDI_MAX_BYTES = 256 };
typedef struct {
	uint64_t timestamp_us;
	uint32_t port_id; /* Backend token for received source; 0 = default output. */
	unsigned size;
	uint8_t data[SDK_MIDI_MAX_BYTES];
} sdk_midi_message_t;
typedef struct {
	unsigned input_devices, output_devices, enabled_inputs;
	int output_available;
	uint64_t received, sent, input_dropped, output_dropped;
} sdk_midi_stats_t;
/* Nonblocking queues, safe from multiple control tasks. 1=received, 0=empty. */
int sdk_midi_read(sdk_midi_message_t *message);
/* 0=queued, negative=invalid/unavailable/full. Delivery occurs on the HOST
 * control thread, not at timestamp_us. Bounded complete SysEx is supported;
 * oversized messages are rejected. A queued message can fail on disconnect. */
int sdk_midi_send(const sdk_midi_message_t *message);
sdk_midi_stats_t sdk_midi_stats(void);
#ifdef __cplusplus
}
#endif
#endif
