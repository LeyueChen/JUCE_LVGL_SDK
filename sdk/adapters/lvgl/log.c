#include <lvgl.h>
#include "sdk/log.h"
#include "log.h"

#if LV_USE_LOG
static void log_print(lv_log_level_t level, const char *message)
{
	(void)level;
	/* Copy into the bounded SDK queue; never wait for the HOST or LogTask. */
	(void)sdk_log_text(message);
}
#endif

void sdk_lvgl_log_init(void)
{
#if LV_USE_LOG
	lv_log_register_print_cb(log_print);
#endif
}
