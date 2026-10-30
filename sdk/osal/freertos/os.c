#include "sdk/os.h"
#include "sdk/core.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
typedef struct { sdk_task_fn entry; void *context; } task_start_t;
static void task_entry(void *arg) {
    task_start_t value = *(task_start_t *)arg;
    vPortFree(arg);
    sdk_core_enter(0);
    value.entry(value.context);
    vTaskDelete(NULL);
}
int sdk_task_create(const sdk_task_config_t *config, sdk_task_fn entry, void *context) {
    if (!config || !entry || config->core_id != 0 || config->priority >= configMAX_PRIORITIES) return -1;
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED && sdk_core_current() != 0) return -1;
    task_start_t *start = pvPortMalloc(sizeof(*start));
    if (!start) return -1;
    start->entry = entry; start->context = context;
    uint32_t words = (config->stack_bytes + sizeof(StackType_t) - 1) / sizeof(StackType_t);
    if (xTaskCreate(task_entry, config->name, words, start, config->priority, NULL) != pdPASS) {
        vPortFree(start); return -1;
    }
    return 0;
}
void sdk_delay_ms(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms) ? pdMS_TO_TICKS(ms) : 1); }
uint64_t sdk_uptime_ms(void) {
    static uint32_t previous;
    static uint64_t epoch;
    taskENTER_CRITICAL();
    uint32_t now = (uint32_t)xTaskGetTickCount();
    if (now < previous) epoch += (UINT64_C(1) << 32);
    previous = now;
    uint64_t result = (epoch + now) * 1000u / configTICK_RATE_HZ;
    taskEXIT_CRITICAL();
    return result;
}
