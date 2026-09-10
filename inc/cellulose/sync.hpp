#ifndef CEL_SYNC_HPP
#define CEL_SYNC_HPP

#include "types.hpp"
#include <cstddef>

namespace cellulose {

/// @brief Assumed hardware cache-line size — used to `alignas` data so that
/// independently mutated fields (chunk voxel arrays, per-chunk locks) never share
/// a line and thus never false-share.
///
/// Hardcoded to 64 (x86-64 and every common ARM). It feeds `alignof(Chunk<>)`, so
/// it must be one fixed value across every translation unit;
/// `std::hardware_destructive_interference_size` is deliberately avoided — it can
/// differ with `-mtune` (GCC even warns about using it this way), and Apple ARM
/// reports 128. Over-aligning to 64 on a 128-byte-line target is still safe, just
/// not maximally spread.
inline constexpr size cache_line_size = 64;

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
