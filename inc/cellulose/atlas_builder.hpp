#ifndef CEL_ATLAS_BUILDER_HPP
#define CEL_ATLAS_BUILDER_HPP

// A size-only rectangle packer: given each tile's pixel dimensions it lays them
// out into one sheet and reports where each landed (as a `TextureAtlas` of
// normalised rects) plus the sheet size. It never sees a pixel — the consumer
// blits its images into the returned rects. Renderer-neutral; no image decode.

#include "texture.hpp"
#include "types.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <utility>
#include <vector>

namespace cellulose {

/// @brief One tile to place: its id and pixel size.
struct TileSize final {
	TextureID id = 0;
	u32 width = 0;
	u32 height = 0;
};

/// @brief The result of packing: a `TextureAtlas` (explicit rects, keyed by id)
/// and the pixel dimensions of the sheet the rects are normalised against.
struct PackedAtlas final {
	TextureAtlas atlas;
	u32 width = 0;
	u32 height = 0;
};

namespace impl {

inline auto next_power_of_two(u32 p_value) -> u32 {
	u32 result = 1;
	while (result < p_value)
		result <<= 1;
	return result;
}

} //namespace impl

/// @brief Shelf-pack `p_tiles` into one sheet. Tiles are placed tallest-first,
/// left to right along a shelf; a tile that will not fit the current row starts
/// a new shelf. The sheet width is the next power of two that holds the widest
/// tile and keeps the layout roughly square; the height is whatever the shelves
/// need (also rounded up to a power of two).
inline auto pack(std::span<const TileSize> p_tiles) -> PackedAtlas {
	PackedAtlas packed;
	if (p_tiles.empty()) {
		packed.width = packed.height = 1;
		return packed;
	}

	std::vector<TileSize> order(p_tiles.begin(), p_tiles.end());
	std::stable_sort(order.begin(), order.end(),
			[](const TileSize &p_a, const TileSize &p_b) { return p_a.height > p_b.height; });

	u64 area = 0;
	u32 widest = 1;
	for (const auto &tile : order) {
		area += static_cast<u64>(tile.width) * tile.height;
		widest = std::max(widest, tile.width);
	}

	u32 sheet_width = impl::next_power_of_two(
			std::max(widest, static_cast<u32>(std::sqrt(static_cast<f64>(area)))));

	u32 pen_x = 0;
	u32 pen_y = 0;
	u32 shelf_height = 0;
	u32 used_height = 0;

	std::vector<std::pair<TextureID, std::array<u32, 4>>> placements; // id -> {x, y, w, h}
	for (const auto &tile : order) {
		if (pen_x + tile.width > sheet_width && pen_x > 0) {
			pen_x = 0;
			pen_y += shelf_height;
			shelf_height = 0;
		}
		placements.push_back({ tile.id, { pen_x, pen_y, tile.width, tile.height } });
		pen_x += tile.width;
		shelf_height = std::max(shelf_height, tile.height);
		used_height = std::max(used_height, pen_y + tile.height);
	}

	const u32 sheet_height = impl::next_power_of_two(std::max<u32>(used_height, 1));

	packed.width = sheet_width;
	packed.height = sheet_height;
	const f32 inv_w = 1.0f / static_cast<f32>(sheet_width);
	const f32 inv_h = 1.0f / static_cast<f32>(sheet_height);
	for (const auto &[id, box] : placements)
		packed.atlas.set_rect(id, UvRect{ static_cast<f32>(box[0]) * inv_w, static_cast<f32>(box[1]) * inv_h, static_cast<f32>(box[0] + box[2]) * inv_w, static_cast<f32>(box[1] + box[3]) * inv_h });
	return packed;
}

/// @brief Lay `p_tile_px`-square tiles out **row-major in a near-square,
/// power-of-two grid** — GPUs store and sample a roughly square sheet far better
/// than a tall 1×N strip (and `GL_MAX_TEXTURE_SIZE` caps a strip's length). Cell
/// `n` holds texture id `n`, so ids need not be contiguous (id `0`, the "unset"
/// sentinel, is just an unused cell). The returned atlas is `Grid` mode, so
/// `rect_of(id)` is cell `(id % columns, id / columns)`; the consumer blits each
/// tile's pixels there. `columns() == width / p_tile_px`.
inline auto pack_grid(std::span<const TextureID> p_ids, u32 p_tile_px) -> PackedAtlas {
	u32 count = 1;
	for (const TextureID id : p_ids)
		count = std::max(count, id + 1);

	u32 columns = 1;
	while (columns * columns < count)
		columns <<= 1;
	u32 rows = impl::next_power_of_two((count + columns - 1) / columns);

	PackedAtlas packed;
	packed.width = columns * p_tile_px;
	packed.height = rows * p_tile_px;
	packed.atlas = TextureAtlas::grid(columns, rows);
	return packed;
}

} //namespace cellulose

#endif // CEL_ATLAS_BUILDER_HPP
