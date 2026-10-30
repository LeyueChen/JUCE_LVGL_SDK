#include <lvgl.h>
#include "sdk/system.h"
#include "sdk/os.h"
#include "sdk/log.h"
#include "sdk/storage.h"
#include "sdk_config.h"
#include "../media/media.h"
#include <string.h>
static lv_obj_t *stats_label;
static lv_obj_t *counter_label;
static unsigned clicks;
static uint64_t last_update;
static void on_monitor(lv_event_t *event)
{
	lv_obj_t *button = lv_event_get_target_obj(event);
	int enabled = lv_obj_has_state(button, LV_STATE_CHECKED);
	user_media_monitor_enable(enabled);
	lv_label_set_text(lv_obj_get_child(button, 0),
			  enabled ? "Audio monitor: ON" : "Audio monitor: OFF");
}
static void on_click(lv_event_t *event)
{
	(void)event;
	++clicks;
	sdk_ui_record_event();
#ifdef CONFIG_SDK_FLASHDB
	sdk_storage_request_t kv = { 0 };
	kv.operation = SDK_STORAGE_KV_SET;
	strcpy(kv.key, "ui.clicks");
	kv.size = sizeof(clicks);
	memcpy(kv.data, &clicks, sizeof(clicks));
	if (sdk_storage_submit(&kv, NULL, NULL))
		sdk_log_text("KV queue is full");
#endif
#ifdef CONFIG_SDK_LITTLEFS
	sdk_storage_request_t file = { 0 };
	file.operation = SDK_STORAGE_FILE_WRITE;
	strcpy(file.key, "/ui-events.bin");
	file.size = sizeof(clicks);
	memcpy(file.data, &clicks, sizeof(clicks));
	if (sdk_storage_submit(&file, NULL, NULL))
		sdk_log_text("File queue is full");
#endif
	lv_label_set_text_fmt(counter_label, "Button events: %u", clicks);
	sdk_log_text("LVGL button clicked on UiTask");
}
void user_ui_create(void)
{
	lv_obj_t *screen = lv_screen_active();
	lv_obj_set_style_bg_color(screen, lv_color_hex(0x101B2C), 0);
	lv_obj_set_style_text_color(screen, lv_color_hex(0xE8F0F8), 0);
	lv_obj_t *title = lv_label_create(screen);
	lv_label_set_text(title, "LVGL / JUCE   -   Portable SDK");
	lv_obj_align(title, LV_ALIGN_TOP_LEFT, 32, 28);
	lv_obj_t *subtitle = lv_label_create(screen);
	lv_label_set_text(
		subtitle,
		"CPU0: FreeRTOS    |    CPU1 / CPU2 / CPU3: RESERVED");
	lv_obj_align(subtitle, LV_ALIGN_TOP_LEFT, 32, 68);
	stats_label = lv_label_create(screen);
	lv_obj_align(stats_label, LV_ALIGN_TOP_LEFT, 32, 120);
	lv_obj_t *button = lv_button_create(screen);
	lv_obj_set_size(button, 224, 56);
	lv_obj_align(button, LV_ALIGN_LEFT_MID, 32, 22);
	lv_obj_add_event_cb(button, on_click, LV_EVENT_CLICKED, NULL);
	lv_obj_t *label = lv_label_create(button);
	lv_label_set_text(label, "Test portable UI event");
	lv_obj_center(label);
#ifdef CONFIG_SDK_AUDIO
	lv_obj_t *monitor_button = lv_button_create(screen);
	lv_obj_set_size(monitor_button, 224, 56);
	lv_obj_align(monitor_button, LV_ALIGN_LEFT_MID, 288, 22);
	lv_obj_add_flag(monitor_button, LV_OBJ_FLAG_CHECKABLE);
	lv_obj_add_event_cb(monitor_button, on_monitor, LV_EVENT_VALUE_CHANGED,
			    NULL);
	lv_obj_t *monitor_label = lv_label_create(monitor_button);
	lv_label_set_text(monitor_label, "Audio monitor: OFF");
	lv_obj_center(monitor_label);
#endif
	counter_label = lv_label_create(screen);
	lv_label_set_text(counter_label, "Button events: 0");
	lv_obj_align(counter_label, LV_ALIGN_BOTTOM_LEFT, 32, -74);
	lv_obj_t *footer = lv_label_create(screen);
	lv_label_set_text(
		footer,
		"user/ owns the product. SDK owns dependencies and platform integration.");
	lv_obj_align(footer, LV_ALIGN_BOTTOM_LEFT, 32, -30);
}
void user_ui_tick(void)
{
	uint64_t now = sdk_uptime_ms();
	if (now - last_update < 250)
		return;
	last_update = now;
	sdk_system_stats_t stats = sdk_system_stats();
	lv_label_set_text_fmt(
		stats_label,
		"Uptime: %lu ms\nApplication heartbeats: %lu\nPublished frames: %lu\nStorage completed: %u / failed: %u",
		(unsigned long)now, (unsigned long)stats.heartbeats,
		(unsigned long)stats.frames, stats.storage_completed,
		stats.storage_failed);
}
