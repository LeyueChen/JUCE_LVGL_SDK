#ifndef SDK_OS_H
#define SDK_OS_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef void (*sdk_task_fn)(void *context);
typedef struct { const char *name; uint32_t stack_bytes; uint32_t priority; uint32_t core_id; } sdk_task_config_t;
int sdk_task_create(const sdk_task_config_t *config, sdk_task_fn entry, void *context);
void sdk_delay_ms(uint32_t milliseconds);
uint64_t sdk_uptime_ms(void);
#ifdef __cplusplus
}
#endif
#endif
