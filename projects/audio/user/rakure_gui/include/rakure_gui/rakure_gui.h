#ifndef RAKURE_GUI_H
#define RAKURE_GUI_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* All calls/callbacks belong to the LVGL owner. Actions are copied at create;
 * context outlives the GUI. No SDK/RTOS/audio-engine types cross this boundary. */
typedef struct {
	int (*monitor_changed)(int enabled, void *context);
	void (*clicked)(unsigned count, void *context);
	void *context;
} rakure_gui_actions_t;
typedef struct {
	uint64_t uptime_ms, heartbeats, frames;
	unsigned storage_completed, storage_failed;
} rakure_gui_status_t;
void rakure_gui_create(const rakure_gui_actions_t *actions);
void rakure_gui_update(const rakure_gui_status_t *status);
void rakure_gui_set_key_status(unsigned button, unsigned action);
#ifdef __cplusplus
}
#endif
#endif
