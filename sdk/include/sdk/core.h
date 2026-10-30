#ifndef SDK_CORE_H
#define SDK_CORE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SDK_CORE_HOST UINT32_MAX
typedef enum {
	SDK_CORE_RESERVED,
	SDK_CORE_STARTING,
	SDK_CORE_RUNNING,
	SDK_CORE_STOPPED
} sdk_core_state_t;
typedef struct sdk_core_context {
	uint32_t logical_id;
	const char *runtime;
} sdk_core_context_t;
typedef void (*sdk_core_entry_t)(const sdk_core_context_t *context);
typedef struct {
	sdk_core_entry_t entries[4];
} sdk_core_roles_t;
uint32_t sdk_core_current(void);
sdk_core_state_t sdk_core_state(uint32_t id);
unsigned sdk_core_entry_count(uint32_t id);
void sdk_core_enter(uint32_t id);
void sdk_core_boot_primary(const sdk_core_roles_t *roles);
void sdk_core_mark_stopped(void);
const sdk_core_roles_t *user_core_roles(void);
#ifdef __cplusplus
}
#endif
#endif
