#include "sdk/host_bridge.h"
#include "sdk/core.h"
#include "rea/rea_audio.h"
#include "sdk/os.h"
#include "sdk/system.h"
#include "sdk/ui.h"
#include "backend.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdbool.h>
#include <stdatomic.h>
#include <math.h>
#include <stdio.h>
#include <windows.h>

/* Windows HOST test timing; project processing code has no Windows dependency. */
static atomic_bool ready, configured;
static atomic_uint phase;

static int configure(rea_chain_t *chain, void *context)
{
	(void)context;
	return rea_chain_parameter(chain, 0, 0, 0.5f);
}

static void completed(int result, void *context)
{
	(void)context;
	configASSERT(!result);
	atomic_store(&configured, true);
}

static void control(void *context)
{
	(void)context;
	configASSERT(!rea_audio_start());
	configASSERT(!rea_audio_control(configure, NULL, completed));
	atomic_store(&ready, true);
	bool started = false, stopped = false;
	while (!sdk_system_is_stopping()) {
		if (atomic_load(&phase) >= 1 && !started) {
			configASSERT(!rea_audio_enable(1));
			started = true;
		}
		if (atomic_load(&phase) >= 2 && !stopped) {
			configASSERT(!rea_audio_enable(0));
			stopped = true;
		}
		sdk_delay_ms(2);
	}
	vTaskDelete(NULL);
}

static void primary(const sdk_core_context_t *context)
{
	(void)context;
	const sdk_task_config_t task = { "EngineControl", 16384, 3, 0 };
	configASSERT(!sdk_task_create(&task, control, NULL));
}

const sdk_core_roles_t *user_core_roles(void)
{
	static const sdk_core_roles_t roles = { { primary, NULL, NULL, NULL } };
	return &roles;
}

void user_ui_create(void)
{
}

void user_ui_tick(void)
{
}

void user_app_start(void)
{
}

int main(int argc, char **argv)
{
	if (argc != 2 || sdk_host_init() || sdk_host_storage_start(argv[1]))
		return 1;
	sdk_runtime_start();
	const ULONGLONG deadline = GetTickCount64() + 10000;
	while (!atomic_load(&ready) || !atomic_load(&configured)) {
		if (GetTickCount64() > deadline)
			return 2;
		Sleep(5);
	}
	sdk_media_audio_start(48000, 1, 2, 128);
	float input[128], left[128], right[128];
	for (unsigned i = 0; i < 128; ++i)
		input[i] = 0.4f;
	const float *inputs[] = { input };
	float *outputs[] = { left, right };
	/* Idle processing leaves the low-level capture/playback APIs usable. */
	sdk_audio_capture_enable(1);
	sdk_media_audio_process(inputs, 1, outputs, 2, 128, 0);
	Sleep(15);
	sdk_audio_packet_t raw = { 0 };
	if (sdk_audio_read(&raw) != 1 || raw.samples[0] != 0.4f)
		return 5;
	sdk_audio_capture_enable(0);
	sdk_audio_format_t format = { 0 };
	sdk_audio_get_format(&format);
	raw.generation = format.generation;
	raw.sample_rate_hz = 48000;
	raw.channels = 2;
	raw.frames = 128;
	for (unsigned i = 0; i < 256; ++i)
		raw.samples[i] = 0.1f;
	if (sdk_audio_write(&raw))
		return 6;
	sdk_media_audio_process(inputs, 1, outputs, 2, 128, 0);
	if (fabsf(left[0] - 0.1f) > 1e-6f)
		return 7;
	atomic_store(&phase, 1);
	while (!rea_audio_enabled()) {
		if (GetTickCount64() > deadline)
			return 8;
		Sleep(2);
	}
	unsigned observed = 0;
	for (unsigned block = 0; block < 100; ++block) {
		sdk_media_audio_process(inputs, 1, outputs, 2, 128,
					block * UINT64_C(2666));
		for (unsigned i = 0; i < 128; ++i) {
			if (fabsf(left[i] - 0.2f) < 1e-6f)
				++observed;
			if (right[i] != 0 || !isfinite(left[i]))
				return 3;
		}
		Sleep(3);
	}
	/* Restart verifies generation changes and rate/channel preparation. */
	sdk_media_audio_stop();
	sdk_media_audio_start(44100, 1, 2, 128);
	for (unsigned i = 0; i < 20; ++i) {
		sdk_media_audio_process(inputs, 1, outputs, 2, 128,
					i * UINT64_C(2902));
		Sleep(3);
	}
	atomic_store(&phase, 2);
	while (rea_audio_enabled()) {
		if (GetTickCount64() > deadline)
			return 9;
		Sleep(2);
	}
	sdk_media_audio_process(inputs, 1, outputs, 2, 128,
				0); /* Flush engine tail. */
	sdk_audio_get_format(&format);
	raw.generation = format.generation;
	raw.sample_rate_hz = 44100;
	if (sdk_audio_write(&raw))
		return 10;
	sdk_media_audio_process(inputs, 1, outputs, 2, 128, 0);
	if (fabsf(left[0] - 0.1f) > 1e-6f)
		return 11;
	sdk_media_audio_stop();
	sdk_runtime_request_stop();
	int joined = sdk_runtime_join(5000);
	sdk_host_storage_stop();
	const sdk_host_stats_t stats = sdk_host_stats();
	if (joined || observed < 128 || stats.affinity_failures ||
	    sdk_core_current() != SDK_CORE_HOST)
		return 4;
	printf("PASS C HOST -> CPU0 AudioTask -> effects -> HOST: %u processed samples\n",
	       observed);
	return 0;
}
