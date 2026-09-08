#ifndef CEL_MORTON_HPP
#define CEL_MORTON_HPP

#include "coordinate.hpp"
#include "types.hpp"
#include <libmorton/morton.h>
#include <cstdint>

namespace cellulose {

/// @brief Morton-encoded index of a cell within a chunk's 1D storage arrays.
using CellIndex = u32;

/// @brief Interleave a local position into its Morton code.
inline auto encode_cell_index(const LocalPosition &p_position) -> CellIndex {
	return static_cast<CellIndex>(libmorton::morton3D_32_encode(
			static_cast<uint_fast16_t>(p_position.x),
			static_cast<uint_fast16_t>(p_position.y),
			static_cast<uint_fast16_t>(p_position.z)));
}

/// @brief De-interleave a Morton code back into a local position.
inline auto decode_cell_index(CellIndex p_index) -> LocalPosition {
	uint_fast16_t x = 0;
	uint_fast16_t y = 0;
	uint_fast16_t z = 0;
	libmorton::morton3D_32_decode(static_cast<uint_fast32_t>(p_index), x, y, z);
	return LocalPosition{
		static_cast<u8>(x),
		static_cast<u8>(y),
		static_cast<u8>(z)
	};
}

} //namespace cellulose

#endif // CEL_MORTON_HPP
