#include "sdk/input_backend.h"
#include <atomic>
namespace
{
std::atomic<uint64_t> state{ 0 };
static_assert(std::atomic<uint64_t>::is_always_lock_free);
}
extern "C" void sdk_input_publish(uint32_t mask)
{
	const auto previous = state.load();
	state.store((previous & 0xffffffff00000000ull) | mask);
}
extern "C" void sdk_input_cancel(void)
{
	state.store((state.load() + (uint64_t(1) << 32)) &
		    0xffffffff00000000ull);
}
extern "C" uint32_t sdk_input_snapshot(uint32_t *generation)
{
	const auto value = state.load();
	*generation = uint32_t(value >> 32);
	return uint32_t(value);
}
