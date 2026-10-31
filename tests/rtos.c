#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "event_groups.h"
#include "stream_buffer.h"
#include "message_buffer.h"
#include "timers.h"
#include "sdk/core.h"
#include "sdk/log.h"
#include "sdk/ui.h"
#include "sdk/input_backend.h"
#include "sdk/input.h"
#include "sdk_config.h"
#include <string.h>
static TaskHandle_t owner;
static StaticTask_t child_tcb;
static StackType_t child_stack[4096];
static QueueHandle_t irq_queue;
static volatile int passed;
static uint32_t interrupt_handler(void)
{
	BaseType_t wake = pdFALSE;
	unsigned value = 321;
	xQueueSendFromISR(irq_queue, &value, &wake);
	return wake != pdFALSE;
}
static void child(void *unused)
{
	(void)unused;
	configASSERT(sdk_core_current() == 0);
	configASSERT(!sdk_log_text("PASS native static task is CPU0"));
	xTaskNotifyGiveIndexed(owner, 1);
	vTaskDelete(NULL);
}
static void ui_callback(const void *payload, size_t size, void *unused)
{
	(void)unused;
	configASSERT(sdk_ui_is_current() && size == 4 &&
		     !memcmp(payload, "UI!", 4));
	xTaskNotifyGive(owner);
}
static void timer_callback(TimerHandle_t timer)
{
	(void)timer;
	configASSERT(sdk_core_current() == 0);
	configASSERT(!sdk_ui_post(ui_callback, "UI!", 4, NULL));
	xTaskNotifyGiveIndexed(owner, 2);
}
static void test_task(void *unused)
{
	(void)unused;
	configASSERT(sdk_core_current() == 0);
	owner = xTaskGetCurrentTaskHandle();
	StaticQueue_t queue_state;
	unsigned queue_bytes[2], item = 123, restored = 0;
	QueueHandle_t queue = xQueueCreateStatic(
		2, sizeof(unsigned), (uint8_t *)queue_bytes, &queue_state);
	QueueSetHandle_t set = xQueueCreateSet(2);
	configASSERT(queue && set && xQueueAddToSet(queue, set) == pdPASS);
	vQueueAddToRegistry(queue, "rtos-test");
	configASSERT(xQueueSend(queue, &item, 0) == pdPASS);
	configASSERT(xQueueSelectFromSet(set, 0) == queue);
	configASSERT(xQueueReceive(queue, &restored, 0) == pdPASS &&
		     restored == item);
	configASSERT(xQueueRemoveFromSet(queue, set) == pdPASS);
	vQueueUnregisterQueue(queue);
	vQueueDelete(queue);
	vQueueDelete(set);
	StaticSemaphore_t mutex_state, count_state;
	SemaphoreHandle_t mutex =
		xSemaphoreCreateRecursiveMutexStatic(&mutex_state);
	SemaphoreHandle_t count =
		xSemaphoreCreateCountingStatic(2, 1, &count_state);
	configASSERT(mutex && count);
	configASSERT(xSemaphoreTakeRecursive(mutex, 0) &&
		     xSemaphoreTakeRecursive(mutex, 0));
	configASSERT(xSemaphoreGiveRecursive(mutex) &&
		     xSemaphoreGiveRecursive(mutex));
	configASSERT(xSemaphoreTake(count, 0) && !xSemaphoreTake(count, 0) &&
		     xSemaphoreGive(count));
	vSemaphoreDelete(mutex);
	vSemaphoreDelete(count);
	StaticEventGroup_t event_state;
	EventGroupHandle_t events = xEventGroupCreateStatic(&event_state);
	configASSERT(events && xEventGroupSetBits(events, 3) == 3);
	configASSERT(xEventGroupWaitBits(events, 3, pdTRUE, pdTRUE, 0) == 3);
	vEventGroupDelete(events);
	StaticStreamBuffer_t stream_state, message_state;
	uint8_t stream_bytes[65], message_bytes[65];
	StreamBufferHandle_t stream =
		xStreamBufferCreateStatic(64, 1, stream_bytes, &stream_state);
	MessageBufferHandle_t message =
		xMessageBufferCreateStatic(64, message_bytes, &message_state);
	char output[8] = { 0 };
	configASSERT(stream && message);
	configASSERT(xStreamBufferSend(stream, "abc", 4, 0) == 4);
	configASSERT(xStreamBufferReceive(stream, output, sizeof(output), 0) ==
			     4 &&
		     !strcmp(output, "abc"));
	configASSERT(xMessageBufferSend(message, "msg", 4, 0) == 4);
	configASSERT(xMessageBufferReceive(message, output, sizeof(output),
					   0) == 4 &&
		     !strcmp(output, "msg"));
	vStreamBufferDelete(stream);
	vMessageBufferDelete(message);
	configASSERT(xTaskCreateStatic(child, "NativeStatic", 4096, NULL, 3,
				       child_stack, &child_tcb));
	configASSERT(ulTaskNotifyTakeIndexed(1, pdTRUE, pdMS_TO_TICKS(2000)) ==
		     1);
	StaticTimer_t timer_state;
	TimerHandle_t timer =
		xTimerCreateStatic("NativeTimer", pdMS_TO_TICKS(20), pdFALSE,
				   NULL, timer_callback, &timer_state);
	configASSERT(timer && xTimerStart(timer, pdMS_TO_TICKS(100)));
	configASSERT(ulTaskNotifyTakeIndexed(2, pdTRUE, pdMS_TO_TICKS(2000)) ==
		     1);
	configASSERT(ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2000)) == 1);
	configASSERT(xTimerDelete(timer, pdMS_TO_TICKS(100)));
	/* Timer command completion must precede release of its static memory. */
	vTaskDelay(pdMS_TO_TICKS(50));
	irq_queue = xQueueCreate(1, sizeof(unsigned));
	configASSERT(irq_queue);
	vPortSetInterruptHandler(3, interrupt_handler);
	vPortGenerateSimulatedInterrupt(3);
	configASSERT(xQueueReceive(irq_queue, &restored, pdMS_TO_TICKS(2000)) &&
		     restored == 321);
	vQueueDelete(irq_queue);
	TaskStatus_t tasks[32];
	configRUN_TIME_COUNTER_TYPE total;
	configASSERT(uxTaskGetSystemState(tasks, 32, &total) > 0);
	HeapStats_t heap;
	vPortGetHeapStats(&heap);
	configASSERT(heap.xAvailableHeapSpaceInBytes > 0);
#ifdef CONFIG_SDK_MULTIBUTTON
	/* Use the same level bridge as JUCE, before interactive input is enabled.
	 * Repeated level publications must still produce one click. */
	sdk_button_event_t event;
	while (sdk_button_read(&event) == 1) {
	}
	sdk_input_publish(1u << SDK_BUTTON_A);
	for (unsigned i = 0; i < 12; ++i) {
		sdk_input_publish(1u << SDK_BUTTON_A);
		vTaskDelay(pdMS_TO_TICKS(CONFIG_SDK_BUTTON_SCAN_MS));
	}
	sdk_input_publish(0);
	vTaskDelay(pdMS_TO_TICKS(CONFIG_SDK_BUTTON_CLICK_MS + 150));
	unsigned presses = 0, clicks = 0;
	while (sdk_button_read(&event) == 1) {
		if (event.action == SDK_BUTTON_PRESS)
			++presses;
		if (event.action == SDK_BUTTON_CLICK)
			++clicks;
	}
	configASSERT(presses == 1 && clicks == 1);
	sdk_input_publish(1u << SDK_BUTTON_B);
	vTaskDelay(pdMS_TO_TICKS(100));
	sdk_input_cancel();
	vTaskDelay(pdMS_TO_TICKS(CONFIG_SDK_BUTTON_CLICK_MS + 150));
	unsigned cancels = 0;
	while (sdk_button_read(&event) == 1) {
		configASSERT(event.action != SDK_BUTTON_CLICK);
		if (event.action == SDK_BUTTON_CANCEL)
			++cancels;
	}
	configASSERT(cancels == 1);
#endif
	passed = 1;
	configASSERT(!sdk_log_text(
		"PASS FreeRTOS tasks/queues/sets/semaphores/events/notifications/timers/streams/messages/ISR/UI/buttons"));
	vTaskDelete(NULL);
}
void sdk_rtos_self_test_start(void)
{
	configASSERT(xTaskCreate(test_task, "RtosContracts", 16384, NULL, 4,
				 NULL) == pdPASS);
	while (!passed)
		vTaskDelay(pdMS_TO_TICKS(5));
}
int sdk_rtos_self_test_passed(void)
{
	return passed;
}
