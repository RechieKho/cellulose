#ifndef CEL_TEXTURE_HPP
#define CEL_TEXTURE_HPP

// Atlas geometry — renderer-neutral UV bookkeeping. The array-texture render
// path (see `raylib.hpp`) addresses tiles by layer index and never consults an
// atlas; `TextureAtlas` exists for laying out the strip image an array texture
// is uploaded from, and for consumers targeting a plain 2-D sheet on another
// renderer. No GPU types, no image decoding.

#include "block.hpp"
#include "types.hpp"
#include <vector>

namespace cellulose {

/// @brief A normalised UV rectangle: `[u0, u1] x [v0, v1]`, origin top-left and
/// y pointing down (the raylib / most-image convention).
struct UvRect final {
	f32 u0 = 0.0f;
	f32 v0 = 0.0f;
	f32 u1 = 1.0f;
	f32 v1 = 1.0f;

	friend auto operator==(const UvRect &, const UvRect &) -> bool = default;

	auto width() const -> f32 { return u1 - u0; }
	auto height() const -> f32 { return v1 - v0; }
};

/// @brief Maps a `TextureID` to its placement. Three ways to build one:
///
/// * `layers(count)` — array mode: `rect_of` is the whole `[0,1]^2` for every
///   id (each tile is its own array layer); `count()` is the layer count.
/// * `grid(columns, rows)` — a uniform 2-D sheet: id `n` occupies cell
///   `(n % columns, n / columns)`.
/// * `TextureAtlas{}` + `set_rect(id, rect)` — an explicit 2-D sheet, e.g. the
///   output of `atlas_builder::pack` for non-uniform tiles.
class TextureAtlas final {
public:
	enum class Mode { Layers,
		Grid,
		Explicit };

private:
	Mode m_mode = Mode::Explicit;
	u32 m_columns = 1;
	u32 m_rows = 1;
	std::vector<UvRect> m_rects; //!< used in `Explicit` mode, indexed by id

public:
	TextureAtlas() = default;

	static auto layers(u32 p_count) -> TextureAtlas {
		TextureAtlas atlas;
		atlas.m_mode = Mode::Layers;
		atlas.m_columns = p_count;
		atlas.m_rows = 1;
		return atlas;
	}

	static auto grid(u32 p_columns, u32 p_rows) -> TextureAtlas {
		TextureAtlas atlas;
		atlas.m_mode = Mode::Grid;
		atlas.m_columns = p_columns == 0 ? 1 : p_columns;
		atlas.m_rows = p_rows == 0 ? 1 : p_rows;
		return atlas;
	}

	auto mode() const -> Mode { return m_mode; }

	/// @brief In `Layers` mode the layer count; in `Grid` mode `columns * rows`;
	/// in `Explicit` mode the number of rects set.
	auto count() const -> u32 {
		switch (m_mode) {
			case Mode::Layers:
				return m_columns;
			case Mode::Grid:
				return m_columns * m_rows;
			default:
				return static_cast<u32>(m_rects.size());
		}
	}

	/// @brief Set the rect for `p_id` (grows the table; switches to `Explicit`).
	auto set_rect(TextureID p_id, UvRect p_rect) -> TextureAtlas & {
		m_mode = Mode::Explicit;
		if (p_id >= m_rects.size())
			m_rects.resize(p_id + 1);
		m_rects[p_id] = p_rect;
		return *this;
	}

	/// @brief The UV rect for `p_id`. Whole `[0,1]^2` in `Layers` mode or for an
	/// unset id; the computed cell in `Grid` mode.
	auto rect_of(TextureID p_id) const -> UvRect {
		switch (m_mode) {
			case Mode::Layers:
				return UvRect{};
			case Mode::Grid: {
				const f32 tw = 1.0f / static_cast<f32>(m_columns);
				const f32 th = 1.0f / static_cast<f32>(m_rows);
				const u32 col = p_id % m_columns;
				const u32 row = (p_id / m_columns) % m_rows;
				const f32 u0 = static_cast<f32>(col) * tw;
				const f32 v0 = static_cast<f32>(row) * th;
				return UvRect{ u0, v0, u0 + tw, v0 + th };
			}
			default:
				return p_id < m_rects.size() ? m_rects[p_id] : UvRect{};
		}
	}
};

} //namespace cellulose

#endif // CEL_TEXTURE_HPP
