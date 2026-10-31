#include "sdk/os.h"
#include "FreeRTOS.h"
#include "task.h"
#include "sdk/system.h"
#include "sdk/log.h"
#include "sdk/core.h"
#include "sdk_config.h"
#include "rea/rea_audio.h"
#include "rakure_gui/rakure_gui.h"
#include "sdk/input.h"
#include "sdk/ui.h"
#include "sdk/storage.h"
#include <string.h>
#ifdef CONFIG_SDK_CJSON
#include <cJSON.h>
#endif
static void app_task(void *context)
{
	(void)context;
	TickType_t wake = xTaskGetTickCount();
	while (!sdk_system_is_stopping()) {
		sdk_app_heartbeat();
#ifdef CONFIG_SDK_CJSON
		cJSON *status = cJSON_CreateObject();
		if (!status || !cJSON_AddNumberToObject(status, "logical_cpu",
							sdk_core_current()))
			sdk_assert_failed(__FILE__, __LINE__);
		char *json = cJSON_PrintUnformatted(status);
		if (!json)
			sdk_assert_failed(__FILE__, __LINE__);
		sdk_log_text(json);
		cJSON_free(json);
		cJSON_Delete(status);
#else
		sdk_log_text("CPU0 application heartbeat");
#endif
		xTaskDelayUntil(&wake, pdMS_TO_TICKS(1000));
	}
	vTaskDelete(NULL);
}
static void key_on_ui(const void *payload, size_t size, void *unused)
{
	(void)unused;
	if (size != sizeof(sdk_button_event_t))
		return;
	const sdk_button_event_t *event = payload;
	rakure_gui_set_key_status((unsigned)event->button,
				  (unsigned)event->action);
}
static void control_task(void *unused)
{
	(void)unused;
	while (!sdk_system_is_stopping()) {
		sdk_midi_message_t message;
		for (unsigned i = 0; i < 16 && sdk_midi_read(&message) == 1;
		     ++i)
			rea_audio_handle_midi(&message);
		sdk_button_event_t event;
		for (unsigned i = 0; i < 16 && sdk_button_read(&event) == 1;
		     ++i) {
			rea_audio_handle_button(&event);
			if (sdk_ui_post(key_on_ui, &event, sizeof(event), 0))
				sdk_log_text("GUI queue full");
		}
		sdk_delay_ms(5);
	}
	vTaskDelete(NULL);
}
void user_app_start(void)
{
#ifdef CONFIG_SDK_AUDIO
	if (rea_audio_start())
		sdk_assert_failed(__FILE__, __LINE__);
#endif
	if (xTaskCreate(app_task, "AppTask", 4096, 0, 2, NULL) != pdPASS ||
	    xTaskCreate(control_task, "ControlTask", 4096, 0, 2, NULL) !=
		    pdPASS)
		sdk_assert_failed(__FILE__, __LINE__);
}
static int monitor_changed(int enabled, void *unused)
{
	(void)unused;
	return rea_audio_enable(enabled);
}
static void clicked(unsigned count, void *unused)
{
	(void)unused;
	sdk_ui_record_event();
#ifdef CONFIG_SDK_FLASHDB
	sdk_storage_request_t kv = { 0 };
	kv.operation = SDK_STORAGE_KV_SET;
	strcpy(kv.key, "ui.clicks");
	kv.size = sizeof(count);
	memcpy(kv.data, &count, sizeof(count));
	if (sdk_storage_submit(&kv, NULL, NULL))
		sdk_log_text("KV queue full");
#endif
#ifdef CONFIG_SDK_LITTLEFS
	sdk_storage_request_t file = { 0 };
	file.operation = SDK_STORAGE_FILE_WRITE;
	strcpy(file.key, "/ui-events.bin");
	file.size = sizeof(count);
	memcpy(file.data, &count, sizeof(count));
	if (sdk_storage_submit(&file, NULL, NULL))
		sdk_log_text("File queue full");
#endif
}
void user_ui_create(void)
{
	const rakure_gui_actions_t actions = {
#ifdef CONFIG_SDK_AUDIO
		monitor_changed,
#else
		NULL,
#endif
		clicked, NULL
	};
	rakure_gui_create(&actions);
}
void user_ui_tick(void)
{
	static uint64_t last_update;
	uint64_t now = sdk_uptime_ms();
	if (now - last_update < 250)
		return;
	last_update = now;
	sdk_system_stats_t stats = sdk_system_stats();
	const rakure_gui_status_t status = { now, stats.heartbeats,
					     stats.frames,
					     stats.storage_completed,
					     stats.storage_failed };
	rakure_gui_update(&status);
}
