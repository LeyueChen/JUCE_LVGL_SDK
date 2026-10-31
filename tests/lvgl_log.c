#include <lvgl.h>
#include "sdk/log.h"
#include "log.h"
#include <stdio.h>
#include <string.h>

static unsigned received;
static int queue_full;
static char captured[512];

/* Replace only the RTOS queue boundary; exercise real LVGL log formatting. */
int sdk_log_text(const char *message)
{
	++received;
	snprintf(captured, sizeof(captured), "%s", message);
	return queue_full ? -1 : 0;
}

int main(void)
{
#if LV_USE_LOG
	lv_init();
	sdk_lvgl_log_init();
	received = 0;
#if LV_LOG_LEVEL <= LV_LOG_LEVEL_WARN
	LV_LOG_WARN("SDK LVGL bridge %d", 42);
	if (received != 1 || !strstr(captured, "SDK LVGL bridge 42"))
		return 1;
	queue_full = 1;
	LV_LOG_WARN("queue full remains nonblocking");
	if (received != 2)
		return 2;
#endif
#if LV_LOG_LEVEL > LV_LOG_LEVEL_TRACE
	received = 0;
	LV_LOG_TRACE("filtered trace");
	if (received)
		return 3;
#endif
	lv_deinit();
#else
	/* Disabled configuration must not require any LVGL or logger symbols. */
	sdk_lvgl_log_init();
	if (received)
		return 4;
#endif
	return 0;
}
