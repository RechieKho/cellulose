#ifndef CEL_SYNC_HPP
#define CEL_SYNC_HPP

#include "types.hpp"
#include <cstddef>
#include <new>

namespace cellulose {

/// @brief Size of a hardware cache line, used to align data so that independently
/// mutated fields (chunk voxel arrays, per-chunk locks) never share a line and
/// thus never trigger false sharing.
///
/// Pinned to a single value per translation unit: `std::hardware_destructive_interference_size`
/// where the standard library exposes it, otherwise the near-universal 64.
#ifdef __cpp_lib_hardware_interference_size
inline constexpr size cache_line_size = std::hardware_destructive_interference_size;
#else
inline constexpr size cache_line_size = 64;
#endif

static_assert(
		cache_line_size >= alignof(std::max_align_t),
		"`cache_line_size` must be at least the maximum fundamental alignment.");

/// @brief Wraps a `T` and over-aligns it to a full cache line, so that two
/// `Padded` members of the same object cannot land on one line.
///
/// Aggregate on purpose: `Padded<std::mutex> lock;` default-constructs the mutex,
/// `Padded<int> counter{ 0 };` aggregate-initialises it. Reach the value through
/// `*padded` / `padded->member` / `padded.value`.
template <typename T>
struct alignas(cache_line_size) Padded final {
	T value{};

	auto operator*() -> T & { return value; }
	auto operator*() const -> const T & { return value; }
	auto operator->() -> T * { return &value; }
	auto operator->() const -> const T * { return &value; }
};

} //namespace cellulose

#endif // CEL_SYNC_HPP
