#ifndef SDK_UI_H
#define SDK_UI_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void sdk_ui_run(void *unused);
void sdk_ui_record_event(void);
enum { SDK_UI_MESSAGE_BYTES = 64 };
/* CPU0 task context only, nonblocking. Payload is copied. Context must outlive
 * delivery; callback runs on UiTask and must not block. Returns -1 when full. */
typedef void (*sdk_ui_callback_fn)(const void *payload, size_t size,
				   void *context);
int sdk_ui_post(sdk_ui_callback_fn callback, const void *payload, size_t size,
		void *context);
int sdk_ui_is_current(void);
/* SDK UiTask pump; applications use post(), not dispatch(). */
void sdk_ui_dispatch(void);
void sdk_ui_bind_current(void);
void user_ui_create(void);
void user_ui_tick(void);
void user_app_start(void);
#ifdef __cplusplus
}
#endif
#endif
