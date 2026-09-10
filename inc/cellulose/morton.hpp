#ifndef CEL_MORTON_HPP
#define CEL_MORTON_HPP

#include "coordinate.hpp"
#include "types.hpp"

namespace cellulose {

/// @brief Morton-encoded index of a cell within a chunk's 1D storage arrays.
using CellIndex = u32;

namespace impl {

// Spread the low 10 bits of `p_value` so each occupies every third bit position
// (the "magic bits" method). Inverse: `morton_gather3`.
inline auto morton_spread3(u32 p_value) -> u32 {
	u32 x = p_value & 0x000003ffu;
	x = (x ^ (x << 16)) & 0xff0000ffu;
	x = (x ^ (x << 8)) & 0x0300f00fu;
	x = (x ^ (x << 4)) & 0x030c30c3u;
	x = (x ^ (x << 2)) & 0x09249249u;
	return x;
}

inline auto morton_gather3(u32 p_value) -> u32 {
	u32 x = p_value & 0x09249249u;
	x = (x ^ (x >> 2)) & 0x030c30c3u;
	x = (x ^ (x >> 4)) & 0x0300f00fu;
	x = (x ^ (x >> 8)) & 0xff0000ffu;
	x = (x ^ (x >> 16)) & 0x000003ffu;
	return x;
}

} //namespace impl

/// @brief Interleave a local position into its Morton code (`x` → bit 0, `y` → 1,
/// `z` → 2). Well-defined for each axis in `[0, 1024)`.
inline auto encode_cell_index(const LocalPosition &p_position) -> CellIndex {
	return impl::morton_spread3(p_position.x) |
			(impl::morton_spread3(p_position.y) << 1) |
			(impl::morton_spread3(p_position.z) << 2);
}

/// @brief De-interleave a Morton code back into a local position.
inline auto decode_cell_index(CellIndex p_index) -> LocalPosition {
	return LocalPosition{
		static_cast<u8>(impl::morton_gather3(p_index)),
		static_cast<u8>(impl::morton_gather3(p_index >> 1)),
		static_cast<u8>(impl::morton_gather3(p_index >> 2))
	};
}

} //namespace cellulose

#endif // CEL_MORTON_HPP
