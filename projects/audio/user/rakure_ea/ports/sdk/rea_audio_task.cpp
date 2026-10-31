#include "rea/rea_audio.h"
#include "sdk/audio.h"
#include "sdk/core.h"
#include "sdk/os.h"
#include "sdk/system.h"
#include "sdk_config.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include <atomic>
#include <algorithm>
#ifdef CONFIG_SDK_AUDIO
namespace
{
struct Command {
	rea_audio_control_fn fn;
	void *context;
	rea_audio_control_completion_fn completion;
};
QueueHandle_t commands;
std::atomic<bool> enabled{ false };
std::atomic<bool> applied_enabled{ false };
void audio_task(void *)
{
	auto *chain = rea_chain_create();
	configASSERT(chain && rea_chain_append_gain(chain, 0.15f) >= 0);
	sdk_audio_packet_t input{}, output{};
	uint32_t generation = 0;
	bool prepared = false, was_enabled = false;
	Command command{};
	while (!sdk_system_is_stopping()) {
		for (unsigned i = 0;
		     i < 8 && xQueueReceive(commands, &command, 0) == pdPASS;
		     ++i) {
			int result = command.fn(chain, command.context);
			if (command.completion)
				command.completion(result, command.context);
		}
		const bool active = enabled.load();
		if (active != was_enabled) {
			sdk_audio_capture_enable(active);
			if (active)
				sdk_audio_playback_mute(0);
			else {
				sdk_audio_playback_clear();
				for (unsigned i = 0;
				     i < CONFIG_SDK_AUDIO_QUEUE_BLOCKS &&
				     sdk_audio_read(&input) == 1;
				     ++i) {
				}
			}
			rea_chain_reset(chain);
			was_enabled = active;
			applied_enabled.store(active);
		}
		if (!active) {
			sdk_delay_ms(1);
			continue;
		}
		sdk_audio_format_t format{};
		if (sdk_audio_get_format(&format) || !format.running ||
		    !format.output_channels) {
			sdk_delay_ms(1);
			continue;
		}
		if (format.generation != generation) {
			generation = format.generation;
			prepared =
				rea_chain_prepare(chain, format.sample_rate_hz,
						  format.output_channels) == 0;
		}
		unsigned processed = 0;
		while (processed < 16 && sdk_audio_read(&input) == 1) {
			++processed;
			if (!active || !prepared ||
			    input.generation != generation)
				continue;
			output.generation = generation;
			output.sample_rate_hz = format.sample_rate_hz;
			output.frames = input.frames;
			output.channels = format.output_channels;
			output.timestamp_us = input.timestamp_us;
			for (unsigned i = 0; i < input.frames; ++i)
				for (unsigned c = 0; c < output.channels; ++c)
					output.samples[i * output.channels + c] =
						c < input.channels ?
							input.samples
								[i * input.channels +
								 c] :
							0;
			if (!rea_chain_process(chain, output.samples,
					       output.frames, output.channels))
				sdk_audio_write(&output);
		}
		sdk_delay_ms(1);
	}
	enabled.store(false);
	applied_enabled.store(false);
	sdk_audio_capture_enable(0);
	sdk_audio_playback_mute(1);
	while (xQueueReceive(commands, &command, 0) == pdPASS)
		if (command.completion)
			command.completion(SDK_MEDIA_UNAVAILABLE,
					   command.context);
	rea_chain_destroy(chain);
	vTaskDelete(nullptr);
}
}
#endif
extern "C" int rea_audio_start(void)
{
#ifdef CONFIG_SDK_AUDIO
	if (sdk_core_current() != 0 || sdk_system_is_stopping())
		return SDK_MEDIA_INVALID;
	if (commands)
		return SDK_MEDIA_BUSY;
	commands = xQueueCreate(8, sizeof(Command));
	const sdk_task_config_t task = { "AudioTask", 65536, 4, 0 };
	configASSERT(commands && !sdk_task_create(&task, audio_task, nullptr));
	return 0;
#else
	return SDK_MEDIA_UNAVAILABLE;
#endif
}
extern "C" int rea_audio_control(rea_audio_control_fn fn, void *context,
				 rea_audio_control_completion_fn completion)
{
#ifdef CONFIG_SDK_AUDIO
	if (!commands || !fn || sdk_core_current() != 0 ||
	    sdk_system_is_stopping())
		return SDK_MEDIA_INVALID;
	Command command{ fn, context, completion };
	return xQueueSend(commands, &command, 0) == pdPASS ? 0 : SDK_MEDIA_FULL;
#else
	(void)fn;
	(void)context;
	(void)completion;
	return SDK_MEDIA_UNAVAILABLE;
#endif
}
extern "C" int rea_audio_enable(int active)
{
#ifdef CONFIG_SDK_AUDIO
	if (!commands || sdk_core_current() != 0 || sdk_system_is_stopping())
		return SDK_MEDIA_INVALID;
	enabled.store(active != 0);
	return 0;
#else
	(void)active;
	return SDK_MEDIA_UNAVAILABLE;
#endif
}
extern "C" int rea_audio_enabled(void)
{
#ifdef CONFIG_SDK_AUDIO
	return applied_enabled.load() ? 1 : 0;
#else
	return 0;
#endif
}
