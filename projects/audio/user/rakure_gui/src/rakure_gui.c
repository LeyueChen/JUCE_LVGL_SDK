#include "rakure_gui/rakure_gui.h"
#include <lvgl.h>
#include <string.h>
static lv_obj_t *stats_label;
static lv_obj_t *counter_label;
static unsigned clicks;
static rakure_gui_actions_t actions;
static void on_monitor(lv_event_t *event)
{
	lv_obj_t *button = lv_event_get_target_obj(event);
	int enabled = lv_obj_has_state(button, LV_STATE_CHECKED);
	if (actions.monitor_changed &&
	    actions.monitor_changed(enabled, actions.context)) {
		enabled = 0;
		lv_obj_remove_state(button, LV_STATE_CHECKED);
	}
	lv_label_set_text(lv_obj_get_child(button, 0),
			  enabled ? "Audio monitor: ON" : "Audio monitor: OFF");
}
static void on_click(lv_event_t *event)
{
	(void)event;
	++clicks;
	if (actions.clicked)
		actions.clicked(clicks, actions.context);
	lv_label_set_text_fmt(counter_label, "Button events: %u", clicks);
}
void rakure_gui_create(const rakure_gui_actions_t *callbacks)
{
	if (callbacks)
		actions = *callbacks;
	else
		memset(&actions, 0, sizeof(actions));
	lv_obj_t *screen = lv_screen_active();
	lv_obj_set_style_bg_color(screen, lv_color_hex(0x101B2C), 0);
	lv_obj_set_style_text_color(screen, lv_color_hex(0xE8F0F8), 0);
	lv_obj_t *title = lv_label_create(screen);
	lv_label_set_text(title, "Rakure Audio Demo / LVGL");
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
	if (actions.monitor_changed) {
		lv_obj_t *monitor_button = lv_button_create(screen);
		lv_obj_set_size(monitor_button, 224, 56);
		lv_obj_align(monitor_button, LV_ALIGN_LEFT_MID, 288, 22);
		lv_obj_add_flag(monitor_button, LV_OBJ_FLAG_CHECKABLE);
		lv_obj_add_event_cb(monitor_button, on_monitor,
				    LV_EVENT_VALUE_CHANGED, NULL);
		lv_obj_t *monitor_label = lv_label_create(monitor_button);
		lv_label_set_text(monitor_label, "Audio monitor: OFF");
		lv_obj_center(monitor_label);
	}
	counter_label = lv_label_create(screen);
	lv_label_set_text(counter_label, "Button events: 0");
	lv_obj_align(counter_label, LV_ALIGN_BOTTOM_LEFT, 32, -74);
	lv_obj_t *footer = lv_label_create(screen);
	lv_label_set_text(
		footer, "Click canvas: A save preset / B load / MIDI CC7 gain");
	lv_obj_align(footer, LV_ALIGN_BOTTOM_LEFT, 32, -30);
}
void rakure_gui_update(const rakure_gui_status_t *status)
{
	if (!status)
		return;
	lv_label_set_text_fmt(
		stats_label,
		"Uptime: %lu ms\nApplication heartbeats: %lu\nPublished frames: %lu\nStorage completed: %u / failed: %u",
		(unsigned long)status->uptime_ms,
		(unsigned long)status->heartbeats,
		(unsigned long)status->frames, status->storage_completed,
		status->storage_failed);
}
void rakure_gui_set_key_status(unsigned button, unsigned action)
{
	lv_label_set_text_fmt(counter_label, "Key %u / action %u", button,
			      action);
}
