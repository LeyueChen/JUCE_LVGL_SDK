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
#ifdef SDK_DIAGNOSTICS
extern "C" void sdk_rtos_self_test_start(void);
#endif
namespace
{
std::atomic<bool> finished{ false };
std::thread scheduler;
struct TaskStart {
	TaskFunction_t entry;
	void *context;
};
DWORD WINAPI task_trampoline(void *opaque)
{
	const auto start = *static_cast<TaskStart *>(opaque);
	HeapFree(GetProcessHeap(), 0, opaque);
	sdk_core_enter(0);
	start.entry(start.context);
	sdk_port_assert("FreeRTOS task returned; call vTaskDelete(NULL)", 0);
	return 0;
}
}
// Applied to every kernel-created task, including native/static tasks, Idle and
// TimerTask. JUCE threads retain SDK_CORE_HOST and never enter the kernel.
extern "C" void *sdk_win_create_task_thread(TaskFunction_t entry, void *context)
{
	auto *start = static_cast<TaskStart *>(
		HeapAlloc(GetProcessHeap(), 0, sizeof(TaskStart)));
	if (!start)
		return nullptr;
	*start = { entry, context };
	auto handle = CreateThread(
		nullptr, 1024, task_trampoline, start,
		CREATE_SUSPENDED | STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
	if (!handle)
		HeapFree(GetProcessHeap(), 0, start);
	return handle;
}
extern "C" uint32_t sdk_port_runtime_counter(void)
{
	LARGE_INTEGER count{}, frequency{};
	QueryPerformanceCounter(&count);
	QueryPerformanceFrequency(&frequency);
	return uint32_t((count.QuadPart / frequency.QuadPart) * 1000000 +
			(count.QuadPart % frequency.QuadPart) * 1000000 /
				frequency.QuadPart);
}
static void boot(void *)
{
#ifdef CONFIG_SDK_CJSON
	// cJSON belongs to the target runtime; the JUCE host uses its own JSON engine.
	cJSON_Hooks hooks = { pvPortMalloc, vPortFree };
	cJSON_InitHooks(&hooks);
#endif
	sdk_services_start();
#ifdef SDK_DIAGNOSTICS
	sdk_rtos_self_test_start();
#endif
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
