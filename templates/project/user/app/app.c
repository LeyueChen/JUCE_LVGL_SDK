#include "sdk/os.h"
#include "FreeRTOS.h"
#include "task.h"
#include "sdk/system.h"
#include "sdk/log.h"
#include "sdk/core.h"
#include "sdk_config.h"
#include "../media/media.h"
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
void user_app_start(void)
{
	user_media_start();
	if (xTaskCreate(app_task, "AppTask", 4096, 0, 2, NULL) != pdPASS)
		sdk_assert_failed(__FILE__, __LINE__);
}
