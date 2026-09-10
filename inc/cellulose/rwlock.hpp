#ifndef CEL_RWLOCK_HPP
#define CEL_RWLOCK_HPP

#include <shared_mutex>
#include <type_traits>
#include <utility>

namespace cellulose {

namespace impl {

/// @brief Read-write lock for rarely-touched, variable-size data.
///
/// Unlike `SeqLock`, this grants exclusive access to a writer, so the protected
/// data may reallocate (e.g. a growing map) without a concurrent reader ever
/// touching freed storage. Suited to the freezing-cold "sparse" attribute tier,
/// where access is infrequent enough that writer starvation is not a concern.
///
/// `read()` / `write()` run their functor under a shared / unique lock and
/// forward its result.
template <typename = void>
class RWLock final {
	mutable std::shared_mutex m_mutex;

public:
	template <typename ReadFn>
	auto read(ReadFn &&p_read) const -> std::invoke_result_t<ReadFn &> {
		const std::shared_lock lock(m_mutex);
		return std::forward<ReadFn>(p_read)();
	}

	template <typename WriteFn>
	auto write(WriteFn &&p_write) -> std::invoke_result_t<WriteFn &> {
		const std::unique_lock lock(m_mutex);
		return std::forward<WriteFn>(p_write)();
	}
};

} //namespace impl

using RWLock = impl::RWLock<>;

} //namespace cellulose

#endif // CEL_RWLOCK_HPP
