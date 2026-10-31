#ifndef SDK_INPUT_H
#define SDK_INPUT_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
	SDK_BUTTON_UP,
	SDK_BUTTON_DOWN,
	SDK_BUTTON_LEFT,
	SDK_BUTTON_RIGHT,
	SDK_BUTTON_ENTER,
	SDK_BUTTON_BACK,
	SDK_BUTTON_A,
	SDK_BUTTON_B,
	SDK_BUTTON_COUNT
} sdk_button_id_t;
typedef enum {
	SDK_BUTTON_PRESS,
	SDK_BUTTON_RELEASE,
	SDK_BUTTON_REPEAT,
	SDK_BUTTON_CLICK,
	SDK_BUTTON_DOUBLE_CLICK,
	SDK_BUTTON_LONG_START,
	SDK_BUTTON_LONG_HOLD,
	SDK_BUTTON_CANCEL
} sdk_button_action_t;
typedef struct {
	sdk_button_id_t button;
	sdk_button_action_t action;
	uint64_t timestamp_ms;
} sdk_button_event_t;
/* One application consumer on CPU0; returns 1 event, 0 empty, -1 unavailable.
 * ButtonTask owns MultiButton. Never call button_ticks() in user code when the
 * SDK button service is enabled. Overflow drops new events and increments stats. */
int sdk_button_read(sdk_button_event_t *event);
uint32_t sdk_button_dropped(void);
#ifdef __cplusplus
}
#endif
#endif
