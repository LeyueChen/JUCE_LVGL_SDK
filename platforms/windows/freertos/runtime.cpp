#include "sdk/host_bridge.h"
#include "sdk/core.h"
#include "sdk/os.h"
#include "sdk_config.h"
#include "FreeRTOS.h"
#include "task.h"
#include <windows.h>
#include <atomic>
#include <thread>
#ifdef CONFIG_SDK_CJSON
#include <cJSON.h>
#endif
extern "C" void sdk_services_start(void);
namespace
{
std::atomic<bool> finished{ false };
std::thread scheduler;
}
static void boot(void *)
{
#ifdef CONFIG_SDK_CJSON
	// cJSON belongs to the target runtime; the JUCE host uses its own JSON engine.
	cJSON_Hooks hooks = { pvPortMalloc, vPortFree };
	cJSON_InitHooks(&hooks);
#endif
	sdk_services_start();
	sdk_core_boot_primary(user_core_roles());
}
extern "C" void sdk_runtime_start(void)
{
	scheduler = std::thread([] {
		const sdk_task_config_t config = { "BootTask", 32768, 6, 0 };
		if (sdk_task_create(&config, boot, nullptr))
			sdk_port_assert(__FILE__, __LINE__);
		vTaskStartScheduler();
		sdk_core_mark_stopped();
		finished = true;
	});
}
extern "C" void sdk_runtime_request_stop(void)
{
	sdk_host_request_stop();
}
extern "C" int sdk_runtime_join(unsigned timeout_ms)
{
	const auto start = GetTickCount64();
	while (!finished && GetTickCount64() - start < timeout_ms)
		Sleep(10);
	if (!finished) {
		if (scheduler.joinable())
			scheduler.detach();
		return -1;
	}
	if (scheduler.joinable())
		scheduler.join();
	return 0;
}
extern "C" void vApplicationMallocFailedHook(void)
{
	sdk_port_assert("FreeRTOS heap exhausted", 0);
}
extern "C" void sdk_platform_end_scheduler(void)
{
	vTaskEndScheduler();
	// No scheduler remains; park this last simulator task until process exit.
	Sleep(INFINITE);
}
