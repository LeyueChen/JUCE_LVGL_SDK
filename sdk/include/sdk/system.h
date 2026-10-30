#ifndef SDK_SYSTEM_H
#define SDK_SYSTEM_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uint64_t frames, heartbeats; unsigned active_cores, reserved_cores, storage_completed, storage_failed; } sdk_system_stats_t;
int sdk_system_is_stopping(void);
void sdk_app_heartbeat(void);
sdk_system_stats_t sdk_system_stats(void);
void sdk_assert_failed(const char *file, int line);
#ifdef __cplusplus
}
#endif
#endif
