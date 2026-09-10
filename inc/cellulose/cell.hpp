#ifndef CEL_CELL_ATTRIBUTE_HPP
#define CEL_CELL_ATTRIBUTE_HPP

#include "block.hpp"
#include "types.hpp"
#include <ankerl/unordered_dense.h>
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

	static constexpr u8 two_bit_mask = 0b11;
	static constexpr u8 front_face_brightness_bit_mask_offset = 0;
	static constexpr u8 back_face_brightness_bit_mask_offset = 2;
	static constexpr u8 bottom_face_brightness_bit_mask_offset = 4;
	static constexpr u8 top_face_brightness_bit_mask_offset = 6;
	static constexpr u8 left_face_brightness_bit_mask_offset = 8;
	static constexpr u8 right_face_brightness_bit_mask_offset = 10;
	static constexpr u8 yaw_bit_mask_offset = 12;
	static constexpr u8 pitch_bit_mask_offset = 14;

	enum Pitch {
		PITCH_NONE = 0b00 << pitch_bit_mask_offset,
		PITCH_UP = 0b01 << pitch_bit_mask_offset,
		PITCH_DOWN = 0b11 << pitch_bit_mask_offset
	};

	enum Yaw {
		YAW_FORWARD = 0b00 << yaw_bit_mask_offset,
		YAW_RIGHT = 0b01 << yaw_bit_mask_offset,
		YAW_BACKWARD = 0b10 << yaw_bit_mask_offset,
		YAW_LEFT = 0b11 << yaw_bit_mask_offset
	};

	BlockID block_id;
	BlockState state;

private:
public:
	constexpr auto get_pitch() const -> Pitch {
		return static_cast<Pitch>(
				state & (two_bit_mask << pitch_bit_mask_offset));
	}

	constexpr auto set_pitch(Pitch p_pitch) -> void {
		const auto mask = two_bit_mask << pitch_bit_mask_offset;
		state = (state & ~mask) | (p_pitch & mask);
	}

	constexpr auto get_yaw() const -> Yaw {
		return static_cast<Yaw>(
				state & (two_bit_mask << yaw_bit_mask_offset));
	}

	constexpr auto set_yaw(Yaw p_yaw) -> void {
		const auto mask = two_bit_mask << yaw_bit_mask_offset;
		state = (state & ~mask) | (p_yaw & mask);
	}

	constexpr auto get_right_face_brightness() const -> u8 {
		return (state & (two_bit_mask << right_face_brightness_bit_mask_offset)) >> right_face_brightness_bit_mask_offset;
	}

	constexpr auto set_right_face_brightness(u8 p_brightness) -> void {
		const auto mask = two_bit_mask << right_face_brightness_bit_mask_offset;
		state = (state & ~mask) | ((p_brightness << right_face_brightness_bit_mask_offset) & mask);
	}

	constexpr auto get_left_face_brightness() const -> u8 {
		return (state & (two_bit_mask << left_face_brightness_bit_mask_offset)) >> left_face_brightness_bit_mask_offset;
	}

	constexpr auto set_left_face_brightness(u8 p_brightness) -> void {
		const auto mask = two_bit_mask << left_face_brightness_bit_mask_offset;
		state = (state & ~mask) | ((p_brightness << left_face_brightness_bit_mask_offset) & mask);
	}

	constexpr auto get_top_face_brightness() const -> u8 {
		return (state & (two_bit_mask << top_face_brightness_bit_mask_offset)) >> top_face_brightness_bit_mask_offset;
	}

	constexpr auto set_top_face_brightness(u8 p_brightness) -> void {
		const auto mask = two_bit_mask << top_face_brightness_bit_mask_offset;
		state = (state & ~mask) | ((p_brightness << top_face_brightness_bit_mask_offset) & mask);
	}

	constexpr auto get_bottom_face_brightness() const -> u8 {
		return (state & (two_bit_mask << bottom_face_brightness_bit_mask_offset)) >> bottom_face_brightness_bit_mask_offset;
	}

	constexpr auto set_bottom_face_brightness(u8 p_brightness) -> void {
		const auto mask = two_bit_mask << bottom_face_brightness_bit_mask_offset;
		state = (state & ~mask) | ((p_brightness << bottom_face_brightness_bit_mask_offset) & mask);
	}

	constexpr auto get_back_face_brightness() const -> u8 {
		return (state & (two_bit_mask << back_face_brightness_bit_mask_offset)) >> back_face_brightness_bit_mask_offset;
	}

	constexpr auto set_back_face_brightness(u8 p_brightness) -> void {
		const auto mask = two_bit_mask << back_face_brightness_bit_mask_offset;
		state = (state & ~mask) | ((p_brightness << back_face_brightness_bit_mask_offset) & mask);
	}

	constexpr auto get_front_face_brightness() const -> u8 {
		return (state & (two_bit_mask << front_face_brightness_bit_mask_offset)) >> front_face_brightness_bit_mask_offset;
	}

	constexpr auto set_front_face_brightness(u8 p_brightness) -> void {
		const auto mask = two_bit_mask << front_face_brightness_bit_mask_offset;
		state = (state & ~mask) | ((p_brightness << front_face_brightness_bit_mask_offset) & mask);
	}
};
static_assert(
		sizeof(HotCellAttribute<>) <= 4,
		"`HotCellAttribute` must be at most 4 byte to maximize element count per cache line.");

/// @brief Cold-tier storage: one dense `CellCount`-length SoA array per attribute
/// type. Every cell always costs `sizeof(Attribute)`, so elements must stay small
/// (≤ 8 bytes) — a larger or rarely-present attribute belongs in
/// `SparseCellAttributeCollection`. Use this for data touched often but not every
/// frame across most of the chunk (e.g. lighting, fluid level).
/// @tparam CellCount The number of cells per attribute.
/// @tparam ...Attributes The type (or component) that describes a cell's attribute.
template <size CellCount, typename... Attributes>
class PackedCellAttributeCollection final {
	static_assert(
			((sizeof(Attributes) <= 8) && ...),
			"`PackedCellAttributeCollection` elements must be at most 8 bytes (dense per-cell arrays). Use `SparseCellAttributeCollection` for larger or sparse attributes.");

public:
	static constexpr size attribute_count = sizeof...(Attributes);

private:
	std::tuple<std::array<Attributes, CellCount>...> m_attributes;

public:
	template <typename Attribute>
	inline auto get() -> std::array<Attribute, CellCount> & {
		return std::get<std::array<Attribute, CellCount>>(m_attributes);
	}

	template <typename Attribute>
	inline auto get() const -> const std::array<Attribute, CellCount> & {
		return std::get<std::array<Attribute, CellCount>>(m_attributes);
	}
};

/// @brief Freezing-cold-tier storage: a per-cell hash map, so only the cells that
/// actually carry the attribute cost memory. Use this for large elements
/// (> 8 bytes) or attributes present on only a few cells (e.g. tile entities).
/// @tparam ...Attributes The type (or component) that describes a cell's attribute.
template <typename... Attributes>
class SparseCellAttributeCollection final {
	static_assert(
			((sizeof(Attributes) > 8) && ...),
			"`SparseCellAttributeCollection` elements must be larger than 8 bytes. Use `PackedCellAttributeCollection` for small dense attributes.");

public:
	static constexpr size attribute_count = sizeof...(Attributes);

private:
	std::tuple<ankerl::unordered_dense::map<size, Attributes>...> m_attributes;

public:
	template <typename Attribute>
	inline auto get() -> ankerl::unordered_dense::map<size, Attribute> & {
		return std::get<ankerl::unordered_dense::map<size, Attribute>>(m_attributes);
	}

	template <typename Attribute>
	inline auto get() const -> const ankerl::unordered_dense::map<size, Attribute> & {
		return std::get<ankerl::unordered_dense::map<size, Attribute>>(m_attributes);
	}
};

} //namespace impl

using HotCellAttribute = impl::HotCellAttribute<>;
template <size CellCount, typename... Attributes>
using PackedCellAttributeCollection = impl::PackedCellAttributeCollection<CellCount, Attributes...>;
template <typename... Attributes>
using SparseCellAttributeCollection = impl::SparseCellAttributeCollection<Attributes...>;
} //namespace cellulose

#endif // CEL_CELL_ATTRIBUTE_HPP