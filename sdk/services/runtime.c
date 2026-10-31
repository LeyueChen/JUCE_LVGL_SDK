#include "sdk/os.h"
#include "sdk/system.h"
#include "sdk/host_bridge.h"
#include "sdk/ui.h"
#include "sdk/log.h"
#include "sdk/core.h"
#include "sdk/storage.h"
#include "sdk_config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include <string.h>
#ifdef CONFIG_SDK_EASYLOGGER
#include <elog.h>
#endif
static QueueHandle_t log_queue;
typedef struct {
	sdk_storage_request_t request;
	sdk_storage_completion_fn completion;
	void *context;
} storage_job_t;
static QueueHandle_t storage_queue;
typedef struct {
	sdk_ui_callback_fn callback;
	void *context;
	size_t size;
	unsigned char payload[SDK_UI_MESSAGE_BYTES];
} ui_message_t;
static QueueHandle_t ui_queue;
static TaskHandle_t ui_owner;
int sdk_ui_post(sdk_ui_callback_fn callback, const void *payload, size_t size,
		void *context)
{
	if (!ui_queue || sdk_core_current() != 0 || !callback ||
	    size > SDK_UI_MESSAGE_BYTES || (size && !payload) ||
	    sdk_system_is_stopping())
		return -1;
	ui_message_t message = { 0 };
	message.callback = callback;
	message.context = context;
	message.size = size;
	if (size)
		memcpy(message.payload, payload, size);
	return xQueueSend(ui_queue, &message, 0) == pdPASS ? 0 : -1;
}
int sdk_ui_is_current(void)
{
	return sdk_core_current() == 0 && ui_owner &&
	       xTaskGetCurrentTaskHandle() == ui_owner;
}
void sdk_ui_bind_current(void)
{
	configASSERT(!ui_owner && sdk_core_current() == 0);
	ui_owner = xTaskGetCurrentTaskHandle();
}
void sdk_ui_dispatch(void)
{
	configASSERT(sdk_ui_is_current());
	ui_message_t message;
	for (unsigned i = 0;
	     i < 16 && xQueueReceive(ui_queue, &message, 0) == pdPASS; ++i)
		message.callback(message.payload, message.size,
				 message.context);
}
void sdk_platform_end_scheduler(void);
void sdk_buttons_start(void);
int sdk_system_is_stopping(void)
{
	return sdk_host_stop_requested();
}
void sdk_app_heartbeat(void)
{
	sdk_host_heartbeat();
}
sdk_system_stats_t sdk_system_stats(void)
{
	sdk_host_stats_t host = sdk_host_stats();
	sdk_system_stats_t result = {
		host.frames, host.heartbeats,	     1,
		3,	     host.storage_completed, host.storage_failed
	};
	return result;
}
void sdk_assert_failed(const char *file, int line)
{
	sdk_port_assert(file, line);
}
void sdk_ui_record_event(void)
{
	sdk_host_ui_event();
}
int sdk_storage_submit(const sdk_storage_request_t *request,
		       sdk_storage_completion_fn completion, void *context)
{
	if (!storage_queue || sdk_core_current() != 0 ||
	    sdk_storage_validate(request) || sdk_system_is_stopping())
		return -1;
	storage_job_t job = { *request, completion, context };
	return xQueueSend(storage_queue, &job, 0) == pdPASS ? 0 : -1;
}
int sdk_log_text(const char *message)
{
	char item[160] = { 0 };
	if (!log_queue || !message || sdk_core_current() != 0)
		return -1;
	strncpy(item, message, sizeof(item) - 1);
	return xQueueSend(log_queue, item, 0) == pdPASS ? 0 : -1;
}
static void log_task(void *unused)
{
	(void)unused;
#ifdef CONFIG_SDK_EASYLOGGER
	sdk_elog_set_sink(sdk_host_log);
	if (elog_init() != ELOG_NO_ERR)
		sdk_port_assert(__FILE__, __LINE__);
	elog_set_fmt(ELOG_LVL_INFO, ELOG_FMT_LVL | ELOG_FMT_TAG);
	elog_start();
#endif
	while (!sdk_host_stop_requested()) {
		char item[160];
		if (xQueueReceive(log_queue, item, pdMS_TO_TICKS(100)) ==
		    pdPASS) {
#ifdef CONFIG_SDK_EASYLOGGER
			elog_i("sdk", "%s", item);
#else
			sdk_host_log(item, strlen(item));
#endif
		}
	}
}
static void storage_task(void *unused)
{
	(void)unused;
	while (!sdk_host_stop_requested() ||
	       uxQueueMessagesWaiting(storage_queue)) {
		storage_job_t job;
		if (xQueueReceive(storage_queue, &job, pdMS_TO_TICKS(50)) ==
		    pdPASS) {
			if (sdk_host_storage_begin(&job.request))
				sdk_port_assert(__FILE__, __LINE__);
			sdk_storage_result_t result;
			while (!sdk_host_storage_finish(&result))
				sdk_delay_ms(2);
			if (job.completion)
				job.completion(&result, job.context);
		}
	}
	sdk_platform_end_scheduler();
}
void sdk_services_start(void)
{
	const sdk_task_config_t ui = { "UiTask", 65536, 3, 0 };
	const sdk_task_config_t log = { "LogTask", 16384, 2, 0 };
	const sdk_task_config_t storage = { "StorageTask", 32768, 1, 0 };
	log_queue = xQueueCreate(16, 160);
	storage_queue = xQueueCreate(8, sizeof(storage_job_t));
	ui_queue = xQueueCreate(16, sizeof(ui_message_t));
	sdk_buttons_start();
	if (!log_queue || !storage_queue || !ui_queue ||
	    sdk_task_create(&ui, sdk_ui_run, 0) ||
	    sdk_task_create(&log, log_task, 0) ||
	    sdk_task_create(&storage, storage_task, 0))
		sdk_port_assert(__FILE__, __LINE__);
}
