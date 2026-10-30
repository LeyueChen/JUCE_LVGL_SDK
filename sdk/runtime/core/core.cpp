#include "sdk/core.h"
#include <array>
#include <atomic>

namespace {
thread_local uint32_t current_core = SDK_CORE_HOST;
std::array<std::atomic<unsigned>, 4> calls{};
std::array<std::atomic<sdk_core_state_t>, 4> states{};
}
extern "C" uint32_t sdk_core_current(void) { return current_core; }
extern "C" void sdk_core_enter(uint32_t id) { current_core = id; }
extern "C" sdk_core_state_t sdk_core_state(uint32_t id) { return id < 4 ? states[id].load() : SDK_CORE_RESERVED; }
extern "C" unsigned sdk_core_entry_count(uint32_t id) { return id < 4 ? calls[id].load() : 0; }
extern "C" void sdk_core_boot_primary(const sdk_core_roles_t *roles) {
    static const sdk_core_context_t context{0, "freertos"};
    sdk_core_enter(0);
    states[0] = SDK_CORE_STARTING;
    ++calls[0];
    roles->entries[0](&context);
    states[0] = SDK_CORE_RUNNING;
}
extern "C" void sdk_core_mark_stopped(void) { states[0] = SDK_CORE_STOPPED; }
