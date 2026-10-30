#ifndef SDK_LOG_H
#define SDK_LOG_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef void (*sdk_log_sink_fn)(const char *, size_t);
void sdk_elog_set_sink(sdk_log_sink_fn sink);
int sdk_log_text(const char *message);
#ifdef __cplusplus
}
#endif
#endif
