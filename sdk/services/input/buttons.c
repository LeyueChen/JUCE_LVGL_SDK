#include "sdk/input.h"
#include "sdk/input_backend.h"
#include "sdk/os.h"
#include "sdk/core.h"
#include "sdk/system.h"
#include "sdk_config.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#ifdef CONFIG_SDK_MULTIBUTTON
#include <multi_button.h>
static Button buttons[SDK_BUTTON_COUNT];
static QueueHandle_t events;
static uint32_t levels, dropped;
static uint8_t read_level(uint8_t id)
{
	return (levels >> id) & 1;
}
static void emit(Button *button, void *unused)
{
	(void)unused;
	sdk_button_event_t event = {
		(sdk_button_id_t)button->button_id,
		(sdk_button_action_t)button_get_event(button), sdk_uptime_ms()
	};
	if (xQueueSend(events, &event, 0) != pdPASS)
		++dropped;
}
static void button_task(void *unused)
{
	(void)unused;
	uint32_t generation = 0, previous = 0;
	for (unsigned i = 0; i < SDK_BUTTON_COUNT; ++i) {
		button_init(&buttons[i], read_level, 1, (uint8_t)i);
		for (unsigned e = 0; e < BTN_EVENT_COUNT; ++e)
			button_attach(&buttons[i], (ButtonEvent)e, emit, 0);
		configASSERT(button_start(&buttons[i]) == 0);
	}
	TickType_t wake = xTaskGetTickCount();
	while (!sdk_system_is_stopping()) {
		levels = sdk_input_snapshot(&generation);
		if (generation != previous) {
			previous = generation;
			for (unsigned i = 0; i < SDK_BUTTON_COUNT; ++i) {
				/* Reset pending single/double-click recognition, too. */
				if (buttons[i].state != BTN_STATE_IDLE) {
					sdk_button_event_t cancel = {
						(sdk_button_id_t)i,
						SDK_BUTTON_CANCEL,
						sdk_uptime_ms()
					};
					if (xQueueSend(events, &cancel, 0) !=
					    pdPASS)
						++dropped;
				}
				button_reset(&buttons[i]);
				buttons[i].button_level = 0;
			}
		}
		button_ticks();
		xTaskDelayUntil(&wake, pdMS_TO_TICKS(TICKS_INTERVAL));
	}
	vTaskDelete(NULL);
}
void sdk_buttons_start(void)
{
	events = xQueueCreate(32, sizeof(sdk_button_event_t));
	const sdk_task_config_t task = { "ButtonTask", 16384, 2, 0 };
	configASSERT(events && sdk_task_create(&task, button_task, 0) == 0);
}
#else
void sdk_buttons_start(void)
{
}
#endif
int sdk_button_read(sdk_button_event_t *event)
{
#ifdef CONFIG_SDK_MULTIBUTTON
	if (!event || !events || sdk_core_current() != 0)
		return -1;
	return xQueueReceive(events, event, 0) == pdPASS ? 1 : 0;
#else
	(void)event;
	return -1;
#endif
}
uint32_t sdk_button_dropped(void)
{
#ifdef CONFIG_SDK_MULTIBUTTON
	if (sdk_core_current() != 0)
		return 0;
	taskENTER_CRITICAL();
	uint32_t value = dropped;
	taskEXIT_CRITICAL();
	return value;
#else
	return 0;
#endif
}
