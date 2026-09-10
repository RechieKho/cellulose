#ifndef CEL_COORDINATE_HPP
#define CEL_COORDINATE_HPP

#include "types.hpp"
#include <ankerl/unordered_dense.h>

namespace cellulose {

/// @brief Position of a cell within a chunk. Each axis is in `[0, chunk_edge_length)`.
struct LocalPosition final {
	u8 x;
	u8 y;
	u8 z;

	friend auto operator==(const LocalPosition &, const LocalPosition &) -> bool = default;
};

/// @brief Position of a chunk in the chunk grid.
struct ChunkPosition final {
	i32 x;
	i32 y;
	i32 z;

	friend auto operator==(const ChunkPosition &, const ChunkPosition &) -> bool = default;
};
static_assert(
		sizeof(ChunkPosition) == 12,
		"`ChunkPosition` must be padding-free so it can be hashed by raw bytes.");

/// @brief Absolute block position in the world.
struct WorldPosition final {
	i64 x;
	i64 y;
	i64 z;

	friend auto operator==(const WorldPosition &, const WorldPosition &) -> bool = default;
};

/// @brief Avalanching hash for `ChunkPosition`, required by `ankerl::unordered_dense::map`.
struct ChunkPositionHash final {
	using is_avalanching = void;

	auto operator()(const ChunkPosition &p_position) const noexcept -> u64 {
		return ankerl::unordered_dense::detail::wyhash::hash(&p_position, sizeof(p_position));
	}
};

/// @brief `log2` of the chunk edge length. Splitting a world coordinate into
/// chunk + local is an arithmetic shift by this / mask with the low bits, which
/// floors correctly for negative operands in C++20. `chunk.hpp` `static_assert`s
/// that `chunk_edge_length == (1 << chunk_edge_length_shift)` — this header must
/// not depend on `chunk.hpp`, so the two are tied from that side.
inline constexpr size chunk_edge_length_shift = 5;
inline constexpr i64 chunk_edge_length_mask = (i64{ 1 } << chunk_edge_length_shift) - 1;

/// @brief The chunk containing `p_world`.
/// @warning The chunk coordinate is narrowed to `i32`; results are only correct
/// while `|p_world.axis| < 2^(31 + chunk_edge_length_shift)` (≈ ±2^36 blocks per
/// axis with the default edge length) — far beyond any practical world.
inline auto to_chunk_position(const WorldPosition &p_world) -> ChunkPosition {
	return ChunkPosition{
		static_cast<i32>(p_world.x >> chunk_edge_length_shift),
		static_cast<i32>(p_world.y >> chunk_edge_length_shift),
		static_cast<i32>(p_world.z >> chunk_edge_length_shift)
	};
}

inline auto to_local_position(const WorldPosition &p_world) -> LocalPosition {
	return LocalPosition{
		static_cast<u8>(p_world.x & chunk_edge_length_mask),
		static_cast<u8>(p_world.y & chunk_edge_length_mask),
		static_cast<u8>(p_world.z & chunk_edge_length_mask)
	};
}

inline auto to_world_position(const ChunkPosition &p_chunk, const LocalPosition &p_local) -> WorldPosition {
	return WorldPosition{
		(static_cast<i64>(p_chunk.x) << chunk_edge_length_shift) | p_local.x,
		(static_cast<i64>(p_chunk.y) << chunk_edge_length_shift) | p_local.y,
		(static_cast<i64>(p_chunk.z) << chunk_edge_length_shift) | p_local.z
	};
}

} //namespace cellulose

#endif // CEL_COORDINATE_HPP
