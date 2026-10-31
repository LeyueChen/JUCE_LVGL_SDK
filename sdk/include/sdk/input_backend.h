#ifndef SDK_INPUT_BACKEND_H
#define SDK_INPUT_BACKEND_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Platform provider: one HOST producer. Bits correspond to sdk_button_id_t.
 * Publish physical levels, not key-repeat events. Cancel on lost focus/disconnect.
 * No kernel calls; the same boundary can be implemented by a GPIO provider. */
void sdk_input_publish(uint32_t pressed_mask);
void sdk_input_cancel(void);
uint32_t sdk_input_snapshot(uint32_t *cancel_generation);
#ifdef __cplusplus
}
#endif
#endif
