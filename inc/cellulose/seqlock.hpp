#ifndef CEL_SEQLOCK_HPP
#define CEL_SEQLOCK_HPP

#include "types.hpp"
#include <atomic>
#include <type_traits>
#include <utility>

namespace cellulose {

namespace impl {

/// @brief Optimistic sequence lock for read-mostly, fixed-size data.
///
/// A writer bumps an atomic counter to odd, mutates the data, then bumps it back
/// to even. A reader snapshots the counter, runs its read, then checks the
/// counter is unchanged and even — retrying otherwise. Readers never block and a
/// writer never waits on a reader, so writers cannot be starved.
///
/// The protected data is touched by `p_read` with the counter possibly odd (a
/// write in flight), so a read may observe a torn value — that snapshot is
/// discarded and retried. This is only sound when every such access stays within
/// fixed-size storage (e.g. a `std::array`), so a torn read cannot fault or go
/// out of bounds. `p_read` must therefore be free of side effects that a retry
/// would make visible, and must not index past fixed bounds.
///
/// `write()` is **not** serialised against other writers; pair the lock with a
/// mutex the writers share if more than one thread may write.
template <typename = void>
class SeqLock final {
	std::atomic<u64> m_sequence{ 0 };

	static_assert(
			std::atomic<u64>::is_always_lock_free,
			"`SeqLock` needs a lock-free 64-bit atomic counter.");

public:
	template <typename WriteFn>
	auto write(WriteFn &&p_write) -> void {
		const auto start = m_sequence.load(std::memory_order_relaxed);
		m_sequence.store(start + 1, std::memory_order_relaxed);
		std::atomic_thread_fence(std::memory_order_release);

		std::forward<WriteFn>(p_write)();

		std::atomic_thread_fence(std::memory_order_release);
		m_sequence.store(start + 2, std::memory_order_relaxed);
	}

	template <typename ReadFn>
	auto read(ReadFn &&p_read) const -> std::invoke_result_t<ReadFn &> {
		using Result = std::invoke_result_t<ReadFn &>;
		static_assert(
				!std::is_reference_v<Result>,
				"a seqlock read functor must return a snapshot by value (or void), "
				"never a reference into the protected data.");

		while (true) {
			u64 before = m_sequence.load(std::memory_order_acquire);
			while (before & 1u)
				before = m_sequence.load(std::memory_order_acquire);

			if constexpr (std::is_void_v<Result>) {
				p_read();
				std::atomic_thread_fence(std::memory_order_acquire);
				if (m_sequence.load(std::memory_order_relaxed) == before)
					return;
			} else {
				Result result = p_read();
				std::atomic_thread_fence(std::memory_order_acquire);
				if (m_sequence.load(std::memory_order_relaxed) == before)
					return result;
			}
		}
	}

	/// @brief Current counter value; even at rest, odd while a write is in flight.
	auto sequence() const -> u64 {
		return m_sequence.load(std::memory_order_acquire);
	}
};

} //namespace impl

using SeqLock = impl::SeqLock<>;

} //namespace cellulose

#endif // CEL_SEQLOCK_HPP
