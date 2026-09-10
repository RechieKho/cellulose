#ifndef CEL_MESH_HPP
#define CEL_MESH_HPP

#include "cell.hpp"
#include "chunk.hpp"
#include "coordinate.hpp"
#include "cursor.hpp"
#include "morton.hpp"
#include "types.hpp"
#include "vector.hpp"
#include "world.hpp"
#include <array>
#include <concepts>
#include <optional>
#include <type_traits>
#include <vector>

namespace cellulose {

/// @brief One vertex of a chunk mesh. Positions are chunk-local, in
/// `[0, chunk_edge_length]`; the consumer offsets by `chunk_position * chunk_edge_length`.
struct MeshVertex final {
	Vec3 position;
	Vec3 normal;
	f32 u; //!< tile-space texture coordinate, `[0, quad_width]`
	f32 v; //!< tile-space texture coordinate, `[0, quad_height]`
	f32 brightness; //!< `[0, 1]`, from the cell's 2-bit face brightness
	u32 block_id;
	TextureID texture_id; //!< render key — array layer / atlas tile; defaults to `block_id`

	friend auto operator==(const MeshVertex &, const MeshVertex &) -> bool = default;
};

/// @brief Triangle geometry for one chunk: `indices` are triples into `vertices`.
struct ChunkMesh final {
	std::vector<MeshVertex> vertices;
	std::vector<u32> indices;

	auto empty() const -> bool { return vertices.empty(); }
};

/// @brief One cell of the grid the greedy mesher runs over. `visible[f]` is `true`
/// when face `f` (`0..5` = `+X -X +Y -Y +Z -Z`) should emit a quad; `brightness`
/// is per face. A cell with no visible face contributes nothing.
struct MeshSample final {
	std::array<bool, 6> visible{};
	u16 block_id = 0;
	std::array<u8, 6> brightness{};
	std::array<TextureID, 6> texture{}; //!< per-face render key; `0` ⇒ fall back to `block_id`
};

namespace impl {

inline auto emit_quad(
		ChunkMesh &p_mesh, i32 p_axis, i32 p_axis_u, i32 p_axis_v, i32 p_sign,
		i32 p_slice, i32 p_u, i32 p_v, i32 p_width, i32 p_height,
		f32 p_scale, u32 p_block_id, TextureID p_texture_id, f32 p_brightness) -> void {
	const f32 plane = static_cast<f32>(p_sign > 0 ? p_slice + 1 : p_slice) * p_scale;

	Vec3 normal{ 0.0f, 0.0f, 0.0f };
	normal[static_cast<size>(p_axis)] = static_cast<f32>(p_sign);

	const std::array<std::array<i32, 2>, 4> corners{
		std::array<i32, 2>{ 0, 0 },
		std::array<i32, 2>{ p_width, 0 },
		std::array<i32, 2>{ p_width, p_height },
		std::array<i32, 2>{ 0, p_height }
	};

	const u32 base = static_cast<u32>(p_mesh.vertices.size());

	for (const auto &corner : corners) {
		Vec3 position{ 0.0f, 0.0f, 0.0f };
		position[static_cast<size>(p_axis)] = plane;
		position[static_cast<size>(p_axis_u)] = static_cast<f32>(p_u + corner[0]) * p_scale;
		position[static_cast<size>(p_axis_v)] = static_cast<f32>(p_v + corner[1]) * p_scale;

		p_mesh.vertices.push_back(MeshVertex{
				position, normal,
				static_cast<f32>(corner[0]), static_cast<f32>(corner[1]),
				p_brightness, p_block_id, p_texture_id });
	}

	const std::array<u32, 6> winding = p_sign > 0
			? std::array<u32, 6>{ 0, 1, 2, 0, 2, 3 }
			: std::array<u32, 6>{ 0, 2, 1, 0, 3, 2 };
	for (const u32 offset : winding)
		p_mesh.indices.push_back(base + offset);
}

} //namespace impl

/// @brief Greedy-mesh a `p_size^3` grid of `MeshSample` (indexed
/// `(x * p_size + y) * p_size + z`). For each face direction and slice it builds a
/// `((texture << 2) | brightness) + 1` key mask over the cells whose face is
/// `visible` (texture = the face's `texture` entry, or `block_id` when that is
/// `0`), merges maximal rectangles, and emits CCW-wound quads scaled by
/// `p_block_scale` (so a downsampled grid still spans `[0, p_size * p_block_scale]`).
/// The emitted `block_id` is that of the merged run's origin cell.
inline auto greedy_mesh(const std::vector<MeshSample> &p_samples, i32 p_size, f32 p_block_scale) -> ChunkMesh {
	ChunkMesh mesh;

	const auto at = [&](i32 p_x, i32 p_y, i32 p_z) -> const MeshSample & {
		return p_samples[static_cast<size>((p_x * p_size + p_y) * p_size + p_z)];
	};

	for (i32 face = 0; face < 6; ++face) {
		const i32 axis = face / 2;
		const i32 sign = (face % 2 == 0) ? 1 : -1;
		const i32 axis_u = (axis + 1) % 3;
		const i32 axis_v = (axis + 2) % 3;

		// Merge key folds in the face's resolved texture id (array layer) and its
		// brightness — never the block id, so unlike blocks that share a face
		// texture still merge. The block id is carried separately for the vertex.
		std::vector<u64> keys(static_cast<size>(p_size) * p_size, 0);
		std::vector<u32> block_at(static_cast<size>(p_size) * p_size, 0);

		for (i32 slice = 0; slice < p_size; ++slice) {
			for (i32 vv = 0; vv < p_size; ++vv)
				for (i32 uu = 0; uu < p_size; ++uu) {
					std::array<i32, 3> cell{};
					cell[static_cast<size>(axis)] = slice;
					cell[static_cast<size>(axis_u)] = uu;
					cell[static_cast<size>(axis_v)] = vv;

					const MeshSample &sample = at(cell[0], cell[1], cell[2]);
					const size fi = static_cast<size>(face);
					const TextureID texture = sample.texture[fi] != 0
							? sample.texture[fi]
							: static_cast<TextureID>(sample.block_id);
					const u64 key = sample.visible[fi]
							? ((static_cast<u64>(texture) << 2) | sample.brightness[fi]) + 1
							: 0;
					keys[static_cast<size>(vv) * p_size + uu] = key;
					block_at[static_cast<size>(vv) * p_size + uu] = sample.block_id;
				}

			for (i32 vv = 0; vv < p_size; ++vv) {
				for (i32 uu = 0; uu < p_size;) {
					const u64 key = keys[static_cast<size>(vv) * p_size + uu];
					if (key == 0) {
						++uu;
						continue;
					}

					i32 width = 1;
					while (uu + width < p_size &&
							keys[static_cast<size>(vv) * p_size + uu + width] == key)
						++width;

					i32 height = 1;
					bool grew = true;
					while (grew && vv + height < p_size) {
						for (i32 x = 0; x < width; ++x)
							if (keys[static_cast<size>(vv + height) * p_size + uu + x] != key) {
								grew = false;
								break;
							}
						if (grew)
							++height;
					}

					impl::emit_quad(
							mesh, axis, axis_u, axis_v, sign, slice, uu, vv, width, height,
							p_block_scale, block_at[static_cast<size>(vv) * p_size + uu],
							static_cast<TextureID>((key - 1) >> 2),
							static_cast<f32>((key - 1) & 0x3u) / 3.0f);

					for (i32 y = 0; y < height; ++y)
						for (i32 x = 0; x < width; ++x)
							keys[static_cast<size>(vv + y) * p_size + uu + x] = 0;

					uu += width;
				}
			}
		}
	}

	return mesh;
}

namespace impl {

/// @brief Face `p_face` brightness of a hot attribute, via the `face_brightness`
/// customization point; flat (full) when the hot type provides no overload.
template <typename HotType>
auto sample_face_brightness(const HotType &p_attribute, i32 p_face) -> u8 {
	if constexpr (requires { face_brightness(p_attribute, p_face); })
		return static_cast<u8>(face_brightness(p_attribute, p_face));
	else
		return 3;
}

/// @brief Face `p_face` texture id of a hot attribute. Uses the explicit
/// resolver `p_texture_of` when the caller supplied one; otherwise the
/// `face_texture` customization point; otherwise the attribute's `block_id`.
template <typename HotType, typename TextureOfPtr>
auto sample_face_texture(const HotType &p_attribute, i32 p_face, TextureOfPtr p_texture_of) -> TextureID {
	if constexpr (std::is_same_v<TextureOfPtr, std::nullptr_t>) {
		if constexpr (requires { face_texture(p_attribute, p_face); })
			return static_cast<TextureID>(face_texture(p_attribute, p_face));
		else
			return static_cast<TextureID>(p_attribute.block_id);
	} else {
		return static_cast<TextureID>((*p_texture_of)(p_attribute, p_face));
	}
}

/// @brief Build the `n^3` `MeshSample` grid for `p_chunk` at LOD `p_level`.
/// A macro-cell has geometry if `p_has_geometry` holds for any of its
/// `(1 << p_level)^3` cells (attributes from the first such cell). Face `f` is
/// visible when the cell has geometry and `p_is_hidden(cell, neighbour)` is false
/// (an absent-chunk neighbour is a default-constructed hot attribute).
template <typename WorldType, typename HasGeometry, typename IsHidden, typename TextureOfPtr>
auto sample_chunk(WorldType &p_world, const ChunkPosition &p_chunk, i32 p_level, HasGeometry &p_has_geometry, IsHidden &p_is_hidden, TextureOfPtr p_texture_of) -> std::vector<MeshSample> {
	using HotType = typename WorldType::HotAttributeType;

	const i32 block = 1 << p_level;
	const i32 n = static_cast<i32>(chunk_edge_length) >> p_level;
	const i32 apron = n + 2;

	const i64 origin_x = static_cast<i64>(p_chunk.x) * static_cast<i64>(chunk_edge_length);
	const i64 origin_y = static_cast<i64>(p_chunk.y) * static_cast<i64>(chunk_edge_length);
	const i64 origin_z = static_cast<i64>(p_chunk.z) * static_cast<i64>(chunk_edge_length);

	ChunkCursor cursor(p_world);

	// The centre chunk supplies the bulk of the reads (every interior cell); take
	// it as one seqlock snapshot instead of ~32 k per-cell acquisitions. The
	// 1-cell apron still falls through to the cursor (neighbour chunks).
	std::vector<HotType> centre;
	bool centre_loaded = false;
	if (auto centre_chunk = p_world.find_chunk(p_chunk); centre_chunk != nullptr) {
		centre.resize(chunk_cell_count);
		centre_chunk->snapshot_hot(centre.data());
		centre_loaded = true;
	}
	const auto hot_at = [&](const WorldPosition &p_cell) -> std::optional<HotType> {
		if (to_chunk_position(p_cell) == p_chunk) {
			if (!centre_loaded)
				return std::nullopt;
			return centre[encode_cell_index(to_local_position(p_cell))];
		}
		return cursor.hot(p_cell);
	};

	// macro-cell attribute (nullopt when the whole block lacks geometry)
	const auto macro = [&](i32 p_mx, i32 p_my, i32 p_mz) -> std::optional<HotType> {
		for (i32 dx = 0; dx < block; ++dx)
			for (i32 dy = 0; dy < block; ++dy)
				for (i32 dz = 0; dz < block; ++dz) {
					const WorldPosition cell{
						origin_x + static_cast<i64>(p_mx) * block + dx,
						origin_y + static_cast<i64>(p_my) * block + dy,
						origin_z + static_cast<i64>(p_mz) * block + dz
					};
					const auto snapshot = hot_at(cell);
					if (snapshot.has_value() && p_has_geometry(*snapshot))
						return snapshot;
				}
		return std::nullopt;
	};

	std::vector<std::optional<HotType>> grid(static_cast<size>(apron) * apron * apron);
	for (i32 x = -1; x <= n; ++x)
		for (i32 y = -1; y <= n; ++y)
			for (i32 z = -1; z <= n; ++z)
				grid[static_cast<size>(((x + 1) * apron + (y + 1)) * apron + (z + 1))] = macro(x, y, z);

	const auto sampled = [&](i32 p_x, i32 p_y, i32 p_z) -> const std::optional<HotType> & {
		return grid[static_cast<size>(((p_x + 1) * apron + (p_y + 1)) * apron + (p_z + 1))];
	};

	static constexpr std::array<std::array<i32, 3>, 6> face_offset{
		std::array<i32, 3>{ 1, 0, 0 }, std::array<i32, 3>{ -1, 0, 0 },
		std::array<i32, 3>{ 0, 1, 0 }, std::array<i32, 3>{ 0, -1, 0 },
		std::array<i32, 3>{ 0, 0, 1 }, std::array<i32, 3>{ 0, 0, -1 }
	};

	std::vector<MeshSample> samples(static_cast<size>(n) * n * n);
	for (i32 x = 0; x < n; ++x)
		for (i32 y = 0; y < n; ++y)
			for (i32 z = 0; z < n; ++z) {
				const auto &self = sampled(x, y, z);
				if (!self.has_value())
					continue;

				MeshSample &out = samples[static_cast<size>((x * n + y) * n + z)];
				out.block_id = self->block_id;
				for (i32 face = 0; face < 6; ++face) {
					out.brightness[static_cast<size>(face)] = sample_face_brightness(*self, face);
					out.texture[static_cast<size>(face)] = sample_face_texture(*self, face, p_texture_of);
					const auto &neighbour = sampled(
							x + face_offset[static_cast<size>(face)][0],
							y + face_offset[static_cast<size>(face)][1],
							z + face_offset[static_cast<size>(face)][2]);
					const HotType far = neighbour.has_value() ? *neighbour : HotType{};
					out.visible[static_cast<size>(face)] = !p_is_hidden(*self, far);
				}
			}

	return samples;
}

/// @brief Recognises a mesher texture resolver — `p_fn(attr, face) -> TextureID`
/// — so it can be told apart from an `is_hidden(near, far)` rule at the same arity.
template <typename Fn, typename HotType>
concept FaceTextureResolver = requires(Fn & p_fn, const HotType &p_attribute) {
	{ p_fn(p_attribute, i32{ 0 }) }->std::same_as<TextureID>;
};

/// @brief Shared body for the four public entry points.
template <typename WorldType, typename HasGeometry, typename IsHidden, typename TextureOfPtr>
auto mesh_chunk_impl(WorldType &p_world, const ChunkPosition &p_chunk, i32 p_level, HasGeometry &p_has_geometry, IsHidden &p_is_hidden, TextureOfPtr p_texture_of) -> ChunkMesh {
	return greedy_mesh(
			impl::sample_chunk(p_world, p_chunk, p_level, p_has_geometry, p_is_hidden, p_texture_of),
			static_cast<i32>(chunk_edge_length) >> p_level,
			static_cast<f32>(1 << p_level));
}

} //namespace impl

/// @brief Greedy-mesh chunk `p_chunk`. `p_is_solid(HotAttribute)` decides both
/// which cells emit geometry and which faces are hidden (a face is culled iff its
/// neighbour is solid). Absent neighbour chunks are empty. Vertices are
/// chunk-local, in `[0, chunk_edge_length]`. Each vertex's `texture_id` comes
/// from the `face_texture` customization point, or the cell's `block_id`.
template <typename WorldType, typename Predicate>
auto mesh_chunk(WorldType &p_world, const ChunkPosition &p_chunk, Predicate &&p_is_solid) -> ChunkMesh {
	auto has_geometry = [&](const auto &p_attribute) { return p_is_solid(p_attribute); };
	auto is_hidden = [&](const auto &, const auto &p_far) { return p_is_solid(p_far); };
	return impl::mesh_chunk_impl(p_world, p_chunk, 0, has_geometry, is_hidden, nullptr);
}

/// @brief `mesh_chunk` with an explicit per-face texture resolver
/// `p_texture_of(attr, face) -> TextureID` (`BlockRegistry` is one). The resolved
/// id also joins the greedy merge key, so faces only merge within one texture.
template <typename WorldType, typename Predicate, typename TextureOf>
requires impl::FaceTextureResolver<TextureOf, typename WorldType::HotAttributeType> auto mesh_chunk(WorldType &p_world, const ChunkPosition &p_chunk, Predicate &&p_is_solid, TextureOf &&p_texture_of) -> ChunkMesh {
	auto has_geometry = [&](const auto &p_attribute) { return p_is_solid(p_attribute); };
	auto is_hidden = [&](const auto &, const auto &p_far) { return p_is_solid(p_far); };
	return impl::mesh_chunk_impl(p_world, p_chunk, 0, has_geometry, is_hidden, &p_texture_of);
}

/// @brief `mesh_chunk` with the geometry and face-culling rules split — for
/// transparency / cutout. `p_has_geometry(attr)` decides whether a cell emits
/// faces; `p_is_hidden(near, far)` decides whether `near`'s face toward `far` is
/// culled (e.g. water-vs-water hidden, water-vs-glass not).
template <typename WorldType, typename HasGeometry, typename IsHidden>
requires(!impl::FaceTextureResolver<IsHidden, typename WorldType::HotAttributeType>) auto mesh_chunk(WorldType &p_world, const ChunkPosition &p_chunk, HasGeometry &&p_has_geometry, IsHidden &&p_is_hidden) -> ChunkMesh {
	return impl::mesh_chunk_impl(p_world, p_chunk, 0, p_has_geometry, p_is_hidden, nullptr);
}

/// @brief `mesh_chunk` with both split rules and an explicit texture resolver.
template <typename WorldType, typename HasGeometry, typename IsHidden, typename TextureOf>
auto mesh_chunk(WorldType &p_world, const ChunkPosition &p_chunk, HasGeometry &&p_has_geometry, IsHidden &&p_is_hidden, TextureOf &&p_texture_of) -> ChunkMesh {
	return impl::mesh_chunk_impl(p_world, p_chunk, 0, p_has_geometry, p_is_hidden, &p_texture_of);
}

/// @brief As `mesh_chunk`, merging each `(1 << p_level)^3` block into one
/// macro-cell (has geometry if any cell does; attributes from the first).
/// `p_level` 0–5; `0` is exactly `mesh_chunk`. Vertices still span `[0, chunk_edge_length]`.
template <typename WorldType, typename Predicate>
auto mesh_chunk_lod(WorldType &p_world, const ChunkPosition &p_chunk, i32 p_level, Predicate &&p_is_solid) -> ChunkMesh {
	auto has_geometry = [&](const auto &p_attribute) { return p_is_solid(p_attribute); };
	auto is_hidden = [&](const auto &, const auto &p_far) { return p_is_solid(p_far); };
	return impl::mesh_chunk_impl(p_world, p_chunk, p_level, has_geometry, is_hidden, nullptr);
}

/// @brief `mesh_chunk_lod` with an explicit per-face texture resolver.
template <typename WorldType, typename Predicate, typename TextureOf>
requires impl::FaceTextureResolver<TextureOf, typename WorldType::HotAttributeType> auto mesh_chunk_lod(WorldType &p_world, const ChunkPosition &p_chunk, i32 p_level, Predicate &&p_is_solid, TextureOf &&p_texture_of) -> ChunkMesh {
	auto has_geometry = [&](const auto &p_attribute) { return p_is_solid(p_attribute); };
	auto is_hidden = [&](const auto &, const auto &p_far) { return p_is_solid(p_far); };
	return impl::mesh_chunk_impl(p_world, p_chunk, p_level, has_geometry, is_hidden, &p_texture_of);
}

/// @brief `mesh_chunk_lod` with split geometry / face-culling rules.
template <typename WorldType, typename HasGeometry, typename IsHidden>
requires(!impl::FaceTextureResolver<IsHidden, typename WorldType::HotAttributeType>) auto mesh_chunk_lod(WorldType &p_world, const ChunkPosition &p_chunk, i32 p_level, HasGeometry &&p_has_geometry, IsHidden &&p_is_hidden) -> ChunkMesh {
	return impl::mesh_chunk_impl(p_world, p_chunk, p_level, p_has_geometry, p_is_hidden, nullptr);
}

/// @brief `mesh_chunk_lod` with both split rules and an explicit texture resolver.
template <typename WorldType, typename HasGeometry, typename IsHidden, typename TextureOf>
auto mesh_chunk_lod(WorldType &p_world, const ChunkPosition &p_chunk, i32 p_level, HasGeometry &&p_has_geometry, IsHidden &&p_is_hidden, TextureOf &&p_texture_of) -> ChunkMesh {
	return impl::mesh_chunk_impl(p_world, p_chunk, p_level, p_has_geometry, p_is_hidden, &p_texture_of);
}

} //namespace cellulose

#endif // CEL_MESH_HPP
