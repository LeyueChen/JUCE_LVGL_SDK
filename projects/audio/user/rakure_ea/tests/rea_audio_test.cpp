#include "sdk/host_bridge.h"
#include "sdk/core.h"
#include "rea/rea_audio.h"
#include "sdk/os.h"
#include "sdk/system.h"
#include "sdk/ui.h"
#include "backend.h"
#include "FreeRTOS.h"
#include "task.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
namespace
{
std::atomic<bool> ready{ false }, configured{ false };
std::atomic<unsigned> phase{ 0 };
int configure(rea_chain_t *chain, void *)
{
	return rea_chain_parameter(chain, 0, 0, 0.5f);
}
void completed(int result, void *)
{
	configASSERT(!result);
	configured = true;
}
void control(void *)
{
	configASSERT(!rea_audio_start());
	configASSERT(!rea_audio_control(configure, nullptr, completed));
	ready = true;
	bool started = false, stopped = false;
	while (!sdk_system_is_stopping()) {
		if (phase.load() >= 1 && !started) {
			configASSERT(!rea_audio_enable(1));
			started = true;
		}
		if (phase.load() >= 2 && !stopped) {
			configASSERT(!rea_audio_enable(0));
			stopped = true;
		}
		sdk_delay_ms(2);
	}
	vTaskDelete(nullptr);
}
void primary(const sdk_core_context_t *)
{
	const sdk_task_config_t task = { "EngineControl", 16384, 3, 0 };
	configASSERT(!sdk_task_create(&task, control, nullptr));
}
}
extern "C" const sdk_core_roles_t *user_core_roles(void)
{
	static const sdk_core_roles_t roles{ { primary, nullptr, nullptr,
					       nullptr } };
	return &roles;
}
extern "C" void user_ui_create(void)
{
}
extern "C" void user_ui_tick(void)
{
}
extern "C" void user_app_start(void)
{
}
int main(int argc, char **argv)
{
	if (argc != 2 || sdk_host_init() || sdk_host_storage_start(argv[1]))
		return 1;
	sdk_runtime_start();
	auto deadline =
		std::chrono::steady_clock::now() + std::chrono::seconds(10);
	while (!ready || !configured) {
		if (std::chrono::steady_clock::now() > deadline)
			return 2;
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	sdk_media_audio_start(48000, 1, 2, 128);
	float input[128], left[128], right[128];
	std::fill_n(input, 128, 0.4f);
	const float *inputs[] = { input };
	float *outputs[] = { left, right };
	// While the SDK engine is idle, low-level capture/playback stays usable.
	sdk_audio_capture_enable(1);
	sdk_media_audio_process(inputs, 1, outputs, 2, 128, 0);
	std::this_thread::sleep_for(std::chrono::milliseconds(15));
	sdk_audio_packet_t raw{};
	if (sdk_audio_read(&raw) != 1 || raw.samples[0] != 0.4f)
		return 5;
	sdk_audio_capture_enable(0);
	sdk_audio_format_t format{};
	sdk_audio_get_format(&format);
	raw.generation = format.generation;
	raw.sample_rate_hz = 48000;
	raw.channels = 2;
	raw.frames = 128;
	std::fill_n(raw.samples, 256, 0.1f);
	if (sdk_audio_write(&raw))
		return 6;
	sdk_media_audio_process(inputs, 1, outputs, 2, 128, 0);
	if (std::abs(left[0] - 0.1f) > 1e-6f)
		return 7;
	phase = 1;
	while (!rea_audio_enabled()) {
		if (std::chrono::steady_clock::now() > deadline)
			return 8;
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	unsigned observed = 0;
	for (unsigned block = 0; block < 100; ++block) {
		sdk_media_audio_process(inputs, 1, outputs, 2, 128,
					block * 2666ull);
		for (unsigned i = 0; i < 128; ++i) {
			if (std::abs(left[i] - 0.2f) < 1e-6f)
				++observed;
			if (right[i] != 0 || !std::isfinite(left[i]))
				return 3;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(3));
	}
	// Restart to verify generation changes and rate/channel preparation.
	sdk_media_audio_stop();
	sdk_media_audio_start(44100, 1, 2, 128);
	for (unsigned i = 0; i < 20; ++i) {
		sdk_media_audio_process(inputs, 1, outputs, 2, 128,
					i * 2902ull);
		std::this_thread::sleep_for(std::chrono::milliseconds(3));
	}
	phase = 2;
	while (rea_audio_enabled()) {
		if (std::chrono::steady_clock::now() > deadline)
			return 9;
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	sdk_media_audio_process(inputs, 1, outputs, 2, 128,
				0); // Flush engine tail.
	sdk_audio_get_format(&format);
	raw.generation = format.generation;
	raw.sample_rate_hz = 44100;
	if (sdk_audio_write(&raw))
		return 10;
	sdk_media_audio_process(inputs, 1, outputs, 2, 128, 0);
	if (std::abs(left[0] - 0.1f) > 1e-6f)
		return 11;
	sdk_media_audio_stop();
	sdk_runtime_request_stop();
	int joined = sdk_runtime_join(5000);
	sdk_host_storage_stop();
	const auto stats = sdk_host_stats();
	if (joined || observed < 128 || stats.affinity_failures ||
	    sdk_core_current() != SDK_CORE_HOST)
		return 4;
	std::printf(
		"PASS HOST -> CPU0 AudioTask -> effects -> HOST: %u processed samples\n",
		observed);
}
