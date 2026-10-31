#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <array>
namespace sdk_media
{
/* Sequence-number bounded MPMC queue. At most eight CAS attempts per call;
 * contention is reported as full/empty rather than waiting in an I/O callback. */
template <typename T, size_t Capacity> class Queue {
	struct Cell {
		std::atomic<size_t> sequence{};
		T value{};
	};
	std::array<Cell, Capacity> cells{};
	alignas(64) std::atomic<size_t> write { 0 };
	alignas(64) std::atomic<size_t> read { 0 };

    public:
	Queue()
	{
		static_assert(Capacity >= 2);
		static_assert(std::atomic<size_t>::is_always_lock_free);
		for (size_t i = 0; i < Capacity; ++i)
			cells[i].sequence.store(i);
	}
	bool push(const T &value)
	{
		auto pos = write.load(std::memory_order_relaxed);
		for (int attempt = 0; attempt < 8; ++attempt) {
			auto &cell = cells[pos % Capacity];
			auto seq =
				cell.sequence.load(std::memory_order_acquire);
			auto difference = static_cast<intptr_t>(seq - pos);
			if (difference < 0)
				return false;
			if (difference == 0 &&
			    write.compare_exchange_weak(
				    pos, pos + 1, std::memory_order_relaxed)) {
				cell.value = value;
				cell.sequence.store(pos + 1,
						    std::memory_order_release);
				return true;
			}
			pos = write.load(std::memory_order_relaxed);
		}
		return false;
	}
	bool pop(T &value)
	{
		auto pos = read.load(std::memory_order_relaxed);
		for (int attempt = 0; attempt < 8; ++attempt) {
			auto &cell = cells[pos % Capacity];
			auto seq =
				cell.sequence.load(std::memory_order_acquire);
			auto difference =
				static_cast<intptr_t>(seq - (pos + 1));
			if (difference < 0)
				return false;
			if (difference == 0 &&
			    read.compare_exchange_weak(
				    pos, pos + 1, std::memory_order_relaxed)) {
				value = cell.value;
				cell.sequence.store(pos + Capacity,
						    std::memory_order_release);
				return true;
			}
			pos = read.load(std::memory_order_relaxed);
		}
		return false;
	}
};
}
