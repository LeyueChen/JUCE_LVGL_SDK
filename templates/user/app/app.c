#include "sdk/os.h"
#include "sdk/system.h"
#include "sdk/log.h"
#include "sdk/core.h"
#include "sdk_config.h"
#ifdef CONFIG_SDK_CJSON
#include <cJSON.h>
#endif
static void app_task(void *context) {
    (void)context;
    while (!sdk_system_is_stopping()) {
        sdk_app_heartbeat();
#ifdef CONFIG_SDK_CJSON
        cJSON *status = cJSON_CreateObject();
        if (!status || !cJSON_AddNumberToObject(status, "logical_cpu", sdk_core_current())) sdk_assert_failed(__FILE__, __LINE__);
        char *json = cJSON_PrintUnformatted(status);
        if (!json) sdk_assert_failed(__FILE__, __LINE__);
        sdk_log_text(json);
        cJSON_free(json);
        cJSON_Delete(status);
#else
        sdk_log_text("CPU0 application heartbeat");
#endif
        sdk_delay_ms(1000);
    }
}
void user_app_start(void) {
    const sdk_task_config_t task = {"AppTask", 16384, 2, 0};
    if (sdk_task_create(&task, app_task, 0)) sdk_assert_failed(__FILE__, __LINE__);
}
