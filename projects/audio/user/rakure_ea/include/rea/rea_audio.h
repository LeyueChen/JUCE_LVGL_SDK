#ifndef REA_AUDIO_H
#define REA_AUDIO_H
#include "rea.h"
#include "sdk/midi.h"
#include "sdk/input.h"
#ifdef __cplusplus
extern "C" {
#endif
/* SDK provider only; the portable chain API is in rea.h. */
int rea_audio_start(void);
void rea_audio_handle_midi(const sdk_midi_message_t *message);
void rea_audio_handle_button(const sdk_button_event_t *event);
/* Project-owned AudioTask integration. Explicitly start it on CPU0 first. CPU0 task callers enqueue commands; command
 * context stays valid through completion. Both callbacks run on AudioTask at a
 * block boundary (or while idle). Do not retain the borrowed chain or block.
 * Only this service consumes captured PCM when enabled; HOST processors and
 * direct sdk_audio_write must not be mixed with it. Default disabled/silent. */
typedef int (*rea_audio_control_fn)(rea_chain_t *chain, void *context);
typedef void (*rea_audio_control_completion_fn)(int result, void *context);
int rea_audio_control(rea_audio_control_fn command, void *context,
		      rea_audio_control_completion_fn completion);
int rea_audio_enable(int enabled);
/* HOST-safe query of the state applied by AudioTask. enable() is asynchronous.
 * Stop external PCM access before enabling; wait for disabled state and one
 * HOST callback to discard pending playback before taking queues back over. */
int rea_audio_enabled(void);
#ifdef __cplusplus
}
#endif
#endif
