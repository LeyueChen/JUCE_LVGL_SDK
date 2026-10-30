#ifndef SDK_HOST_BRIDGE_H
#define SDK_HOST_BRIDGE_H
#include <stddef.h>
#include <stdint.h>
#include "sdk/storage.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
	int x, y, pressed;
	int cancelled;
} sdk_pointer_t;
typedef struct {
	uint32_t group, processor, bound_threads, affinity_failures;
	uint64_t frames, heartbeats;
	unsigned primary_entries, secondary_entries;
	unsigned ui_events, storage_completed, storage_failed;
	uint64_t pointer_pressed_reads;
	uint64_t pointer_cancelled_reads;
} sdk_host_stats_t;
int sdk_host_init(void);
void sdk_host_request_stop(void);
int sdk_host_stop_requested(void);
void sdk_host_set_pointer(int x, int y, int pressed);
/* HOST producer cancels a gesture; UiTask acknowledges it before any newer press. */
void sdk_host_cancel_pointer(void);
sdk_pointer_t sdk_host_pointer(void);
void sdk_host_publish_frame(const void *pixels, size_t bytes);
int sdk_host_copy_frame(void *pixels, size_t bytes);
sdk_host_stats_t sdk_host_stats(void);
void sdk_host_heartbeat(void);
void sdk_host_ui_event(void);
int sdk_host_storage_start(const char *directory);
void sdk_host_storage_stop(void);
int sdk_host_storage_begin(const sdk_storage_request_t *request);
int sdk_host_storage_finish(sdk_storage_result_t *result);
void sdk_host_log(const char *text, size_t length);
size_t sdk_host_drain_log(char *text, size_t capacity);
void sdk_win_bind_thread(void *thread);
void sdk_port_assert(const char *file, int line);
void sdk_runtime_start(void);
void sdk_runtime_request_stop(void);
int sdk_runtime_join(unsigned timeout_ms);
#ifdef __cplusplus
}
#endif
#endif
