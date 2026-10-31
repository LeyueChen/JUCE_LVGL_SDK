#include "sdk/host_bridge.h"
#include "sdk/core.h"
#include "sdk_config.h"
#include "storage_backend.h"
#include <windows.h>
#include <array>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <thread>

namespace
{
constexpr size_t frame_bytes =
	CONFIG_SDK_SCREEN_WIDTH * CONFIG_SDK_SCREEN_HEIGHT * 4ull;
struct Frame {
	std::atomic<int> state{ 0 };
	uint64_t generation{};
	std::array<unsigned char, frame_bytes> bytes{};
};
std::array<Frame, 3> frames;
std::atomic<bool> stopping{ false };
// One HOST producer, one UiTask consumer. Publish positions and cancel generation
// together so input never mixes coordinates from two window positions.
std::atomic<uint64_t> pointer_state{ 0 };
static_assert(std::atomic<uint64_t>::is_always_lock_free);
static_assert(CONFIG_SDK_SCREEN_WIDTH <= 65535 &&
	      CONFIG_SDK_SCREEN_HEIGHT <= 65535);
std::atomic<unsigned> bound{ 0 }, failures{ 0 };
std::atomic<uint64_t> published{ 0 }, beats{ 0 };
std::atomic<uint64_t> pressed_reads{ 0 };
std::atomic<uint64_t> cancelled_reads{ 0 };
std::atomic<unsigned> ui_events{ 0 }, storage_completed{ 0 },
	storage_failed{ 0 };
std::atomic<int> storage_state{ 0 };
std::atomic<bool> storage_stop{ false };
sdk_storage_request_t storage_request{};
sdk_storage_result_t storage_result{};
std::thread storage_worker;
GROUP_AFFINITY affinity{};
unsigned processor = 0;
// Single producer (SDK LogTask), single consumer (JUCE message thread).
std::array<char, 16384> logs{};
std::atomic<size_t> log_write{ 0 }, log_read{ 0 };
}
extern "C" int sdk_host_init(void)
{
	GROUP_AFFINITY current{};
	DWORD_PTR allowed = 0, system = 0;
	if (!GetThreadGroupAffinity(GetCurrentThread(), &current) ||
	    !GetProcessAffinityMask(GetCurrentProcess(), &allowed, &system) ||
	    !allowed)
		return -1;
	affinity.Group = current.Group;
	while (!(allowed & (DWORD_PTR(1) << processor)))
		++processor;
	if (const char *requested = std::getenv("SDK_HOST_PROCESSOR")) {
		char *end = nullptr;
		long value = std::strtol(requested, &end, 10);
		if (*end || value < 0 || value >= sizeof(DWORD_PTR) * 8 ||
		    !(allowed & (DWORD_PTR(1) << value)))
			return -1;
		processor = unsigned(value);
	}
	affinity.Mask = DWORD_PTR(1) << processor;
	std::printf("HOST affinity: logical CPU0 -> group %u processor %u\n",
		    affinity.Group, processor);
	return 0;
}
extern "C" void sdk_win_bind_thread(void *thread)
{
	GROUP_AFFINITY actual{};
	if (!SetThreadGroupAffinity(HANDLE(thread), &affinity, nullptr) ||
	    !GetThreadGroupAffinity(HANDLE(thread), &actual) ||
	    actual.Group != affinity.Group || actual.Mask != affinity.Mask) {
		++failures;
		sdk_port_assert("CPU0 thread affinity", int(GetLastError()));
	}
	++bound;
}
extern "C" void sdk_port_assert(const char *file, int line)
{
	std::fprintf(stderr, "SDK ASSERT %s:%d\n", file, line);
	std::fflush(stderr);
	std::abort();
}
extern "C" void sdk_host_request_stop(void)
{
	stopping.store(true);
}
extern "C" int sdk_host_stop_requested(void)
{
	return stopping.load();
}
extern "C" void sdk_host_set_pointer(int x, int y, int pressed)
{
	const auto previous = pointer_state.load();
	const auto px = uint64_t(std::clamp(x, 0, CONFIG_SDK_SCREEN_WIDTH - 1));
	const auto py =
		uint64_t(std::clamp(y, 0, CONFIG_SDK_SCREEN_HEIGHT - 1));
	pointer_state.store((previous & ~((uint64_t(1) << 33) - 1)) | px |
			    (py << 16) | (uint64_t(pressed != 0) << 32));
}
extern "C" void sdk_host_cancel_pointer(void)
{
	const auto previous = pointer_state.load();
	pointer_state.store((previous + (uint64_t(1) << 33)) &
			    ~(uint64_t(1) << 32));
}
extern "C" sdk_pointer_t sdk_host_pointer(void)
{
	static uint64_t last_cancel = 0; // UiTask owns acknowledgement.
	const auto state = pointer_state.load();
	const auto generation = state >> 33;
	sdk_pointer_t value{ int(state & 0xffff), int((state >> 16) & 0xffff),
			     int((state >> 32) & 1),
			     generation != last_cancel };
	last_cancel = generation;
	if (value.cancelled)
		++cancelled_reads;
	if (value.pressed && !value.cancelled)
		++pressed_reads;
	return value;
}
extern "C" void sdk_host_publish_frame(const void *pixels, size_t bytes)
{
	if (bytes != frame_bytes)
		return;
	for (int desired : { 0, 2 }) {
		for (auto &frame : frames) {
			int expected = desired;
			if (frame.state.compare_exchange_strong(expected, 1)) {
				std::memcpy(frame.bytes.data(), pixels, bytes);
				frame.generation = published.fetch_add(1) + 1;
				frame.state.store(2, std::memory_order_release);
				return;
			}
		}
	}
}
extern "C" int sdk_host_copy_frame(void *pixels, size_t bytes)
{
	if (bytes != frame_bytes)
		return 0;
	// Drain READY slots in one poll; no slot can be overwritten while READING.
	int copied = 0;
	static uint64_t delivered = 0; // consumer-owned
	for (auto &frame : frames) {
		int expected = 2;
		if (frame.state.compare_exchange_strong(
			    expected, 3, std::memory_order_acquire)) {
			if (frame.generation > delivered) {
				std::memcpy(pixels, frame.bytes.data(), bytes);
				delivered = frame.generation;
				copied = 1;
			}
			frame.state.store(0, std::memory_order_release);
		}
	}
	return copied;
}
extern "C" sdk_host_stats_t sdk_host_stats(void)
{
	return { affinity.Group,
		 processor,
		 bound.load(),
		 failures.load(),
		 published.load(),
		 beats.load(),
		 sdk_core_entry_count(0),
		 sdk_core_entry_count(1) + sdk_core_entry_count(2) +
			 sdk_core_entry_count(3),
		 ui_events.load(),
		 storage_completed.load(),
		 storage_failed.load(),
		 pressed_reads.load(),
		 cancelled_reads.load() };
}
extern "C" void sdk_host_heartbeat(void)
{
	++beats;
}
extern "C" void sdk_host_ui_event(void)
{
	++ui_events;
}
extern "C" int sdk_host_storage_start(const char *directory)
{
	std::promise<bool> opened;
	auto ready = opened.get_future();
	storage_worker = std::thread([directory = std::string(directory),
				      signal = std::move(opened)]() mutable {
		void *backend = sdk_backend_open(directory.c_str());
		signal.set_value(backend != nullptr);
		if (!backend)
			return;
		while (!storage_stop.load()) {
			if (storage_state.load(std::memory_order_acquire) !=
			    1) {
				Sleep(2);
				continue;
			}
			const auto &request = storage_request;
			storage_result = {};
			sdk_backend_execute(backend, &request, &storage_result);
			if (storage_result.result < 0)
				++storage_failed;
			else
				++storage_completed;
			storage_state.store(2, std::memory_order_release);
		}
		sdk_backend_close(backend);
	});
	return ready.get() ? 0 : -1;
}
extern "C" void sdk_host_storage_stop(void)
{
	storage_stop = true;
	if (storage_worker.joinable())
		storage_worker.join();
}
extern "C" int sdk_host_storage_begin(const sdk_storage_request_t *request)
{
	if (storage_state.load(std::memory_order_acquire) != 0)
		return -1;
	storage_request = *request;
	storage_state.store(1, std::memory_order_release);
	return 0;
}
extern "C" int sdk_host_storage_finish(sdk_storage_result_t *result)
{
	if (storage_state.load(std::memory_order_acquire) != 2)
		return 0;
	*result = storage_result;
	storage_state.store(0, std::memory_order_release);
	return 1;
}
extern "C" void sdk_host_log(const char *text, size_t size)
{
	const size_t write = log_write.load(std::memory_order_relaxed);
	const size_t read = log_read.load(std::memory_order_acquire);
	if (size > logs.size() - (write - read))
		return; // bounded drop, never block a simulator task
	for (size_t i = 0; i < size; ++i)
		logs[(write + i) % logs.size()] = text[i];
	log_write.store(write + size, std::memory_order_release);
}
extern "C" size_t sdk_host_drain_log(char *text, size_t capacity)
{
	const size_t read = log_read.load(std::memory_order_relaxed);
	const size_t write = log_write.load(std::memory_order_acquire);
	const size_t size = (write - read) < capacity ? (write - read) :
							capacity;
	for (size_t i = 0; i < size; ++i)
		text[i] = logs[(read + i) % logs.size()];
	log_read.store(read + size, std::memory_order_release);
	return size;
}
