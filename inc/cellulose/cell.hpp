#ifndef CEL_CELL_ATTRIBUTE_HPP
#define CEL_CELL_ATTRIBUTE_HPP

#include "block.hpp"
#include "types.hpp"
#include <array>
#include <tuple>

namespace cellulose {

namespace impl {
/// @brief Cell attributes designed to be access frequently on frame-by-frame basis and thus must be dense to maximize element count per cache line.
template <typename = void>
class HotCellAttribute final {
public:
	/// @brief A densely packed data pack that describes the state of the block in the cell.
	/// The block state should describe the orientation and brightness of each face.
	///
	/// The orientation is based on Raylib's coordinate system, in which:
	/// - +X : Right
	/// - -X : Left
	/// - +Y : Up
	/// - -Y : Down
	/// - +Z : Backward
	/// - -Z : Forward
	///
	/// The orientation of the block consists of pitch (rotated on X-axis) and yaw (rotated on Y-axis).
	/// For pitch, there are 3 possible states, which are:
	/// - pitching up (looking to +Y, by rotating anti-clockwise 90-degree viewing from +X),
	/// - no pitching, and
	/// - pitching down (looking to -Y, by rotatiang clockwise 90-degree viewing from +X).
	/// It could be represented by at least 2 bits, or 4 states.
	///
	/// For yaw, there are 4 possible states, which are:
	/// - looking forward (+Z, by rotating clockwise 0-degree viewing from +Y),
	/// - right (+X, by rotating clockwise 90-degree viewing from +Y),
	/// - backward (-Z, by rotating clockwise 180-degree viewing from +Y), and
	/// - left (-X, by rotating clockwise 270-degree viewing from +Y).
	/// It could be represented by exactly 2 bits.
	///
	/// Thus, overall orientation would take up 12 states, and require at least 4 bits (16 states) to contains.
	///
	/// This left us with 12 bits to describe brightness that distribute across each faces.
	/// There are 6 faces, this left us 2 bits per face (or 4 brightness level).
	///
	/// Thus, the bit mapping would be:
	/// --- MSB ---
	/// | pitch (2 bits) |
	/// | yaw (2 bits) |
	/// | Right face (+X) brightness (2 bits) |
	/// | Left face (-X) brightness (2 bits) |
	/// | Top face (+Y) brightness (2 bits) |
	/// | Bottom face (-Y) brightness (2 bits) |
	/// | Back face (+Z) brightness (2 bits) |
	/// | Front face (-Z) brightness (2 bits) |
	/// --- LSB ---
	using BlockState = u16;

	BlockID block_id;
	BlockState state;

private:
public:
	constexpr auto get_pitch() const -> u8 {
		THROW_UNIMPLEMENTED();
		return 0;
	}

	constexpr auto get_yaw() const -> u8 {
		THROW_UNIMPLEMENTED();
		return 0;
	}

	constexpr auto get_right_face_brightness() const -> u8 {
		THROW_UNIMPLEMENTED();
		return 0;
	}

	constexpr auto get_left_face_brightness() const -> u8 {
		THROW_UNIMPLEMENTED();
		return 0;
	}

	constexpr auto get_top_face_brightness() const -> u8 {
		THROW_UNIMPLEMENTED();
		return 0;
	}

	constexpr auto get_bottom_face_brightness() const -> u8 {
		THROW_UNIMPLEMENTED();
		return 0;
	}

	constexpr auto get_back_face_brightness() const -> u8 {
		THROW_UNIMPLEMENTED();
		return 0;
	}

	constexpr auto get_front_face_brightness() const -> u8 {
		THROW_UNIMPLEMENTED();
		return 0;
	}
};
static_assert(
		sizeof(HotCellAttribute<>) <= 4,
		"`HotCellAttribute` must be at most 4 byte to maximize element count per cache line.");

/// @brief  Collection of user-defined cell attribute designed to be access regularly and thus grouped based on type to reduce cache pollution.
/// @tparam CellCount The number of cell per attribute.
/// @tparam Attributes The type (or component) that describe a cell's attribute.
template <size CellCount, typename... Attributes>
class ColdCellAttributeCollection final {
	static_assert(
			((sizeof(Attributes) <= 8) && ...),
			"Type in `Attributes` must be at most 8 byte.");

public:
	static constexpr size attribute_count = sizeof...(Attributes);

private:
	std::tuple<std::array<Attributes, CellCount>...> m_attributes;

public:
	template <typename Attribute>
	inline auto get() -> std::array<Attribute, CellCount> {
		return std::get<std::array<Attribute, CellCount>>(m_attributes);
	}
};
} //namespace impl

using HotCellAttribute = impl::HotCellAttribute<>;
template <size CellCount, typename... Attributes>
using ColdCellAttributeCollection = impl::ColdCellAttributeCollection<CellCount, Attributes...>;
} //namespace cellulose

#endif // CEL_CELL_ATTRIBUTE_HPP