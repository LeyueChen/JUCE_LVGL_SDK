#include "media.h"
#include "sdk/audio.h"
#include "sdk/midi.h"
#include "sdk/os.h"
#include "sdk/log.h"
#include "sdk/system.h"
#include "sdk_config.h"
/* This callback executes in the audio backend's realtime context. Its binding
 * has static lifetime; the CPU0 task never mutates its context. */
static void monitor(const sdk_audio_process_block_t *b, void *unused)
{
	(void)unused;
	for (unsigned c = 0; c < b->output_channels; ++c)
		for (unsigned i = 0; i < b->frames; ++i)
			if (b->output && b->output[c])
				b->output[c][i] =
					c < b->input_channels && b->input &&
							b->input[c] ?
						b->input[c][i] * 0.15f :
						0.0f;
}
static const sdk_audio_processor_t processor = { monitor, 0 };
void user_media_monitor_enable(int enabled)
{
	sdk_audio_set_processor(enabled ? &processor : 0);
}
static void media_task(void *unused)
{
	(void)unused;
	while (!sdk_system_is_stopping()) {
		sdk_midi_message_t message;
		for (unsigned i = 0; i < 16 && sdk_midi_read(&message) == 1;
		     ++i)
			sdk_log_text(
				"MIDI received on CPU0 (no automatic MIDI echo)");
		sdk_delay_ms(5);
	}
}
void user_media_start(void)
{
#ifdef CONFIG_SDK_MIDI
	const sdk_task_config_t task = { "MediaTask", 16384, 2, 0 };
	if (sdk_task_create(&task, media_task, 0))
		sdk_assert_failed(__FILE__, __LINE__);
#endif
}
