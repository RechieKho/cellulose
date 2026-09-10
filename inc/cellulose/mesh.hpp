#ifndef CEL_MESH_HPP
#define CEL_MESH_HPP

#include "cell.hpp"
#include "chunk.hpp"
#include "coordinate.hpp"
#include "morton.hpp"
#include "types.hpp"
#include "vector.hpp"
#include "world.hpp"
#include <array>
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

	friend auto operator==(const MeshVertex &, const MeshVertex &) -> bool = default;
};

/// @brief Triangle geometry for one chunk: `indices` are triples into `vertices`.
struct ChunkMesh final {
	std::vector<MeshVertex> vertices;
	std::vector<u32> indices;

	auto empty() const -> bool { return vertices.empty(); }
};

/// @brief One cell of the local grid the greedy mesher runs over. `brightness`
/// is indexed by face `0..5` = `+X -X +Y -Y +Z -Z`.
struct MeshSample final {
	bool solid = false;
	u16 block_id = 0;
	std::array<u8, 6> brightness{};
};

namespace impl {

inline auto emit_quad(
		ChunkMesh &p_mesh, i32 p_axis, i32 p_axis_u, i32 p_axis_v, i32 p_sign,
		i32 p_slice, i32 p_u, i32 p_v, i32 p_width, i32 p_height,
		f32 p_scale, u32 p_block_id, f32 p_brightness) -> void {
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
				p_brightness, p_block_id });
	}

	const std::array<u32, 6> winding = p_sign > 0
			? std::array<u32, 6>{ 0, 1, 2, 0, 2, 3 }
			: std::array<u32, 6>{ 0, 2, 1, 0, 3, 2 };
	for (const u32 offset : winding)
		p_mesh.indices.push_back(base + offset);
}

} //namespace impl

/// @brief Greedy-mesh an `(p_size + 2)^3` apron grid whose interior is `p_size^3`.
///
/// `p_samples` is indexed `((x + 1) * stride + (y + 1)) * stride + (z + 1)` with
/// `stride = p_size + 2` and `x, y, z` in `-1 .. p_size` (the `-1` / `p_size`
/// shell only needs `solid` set, for neighbour culling). Every quad — position
/// and size — is multiplied by `p_block_scale`, so a downsampled grid still spans
/// `[0, p_size * (1 << level)]`.
inline auto greedy_mesh(const std::vector<MeshSample> &p_samples, i32 p_size, f32 p_block_scale) -> ChunkMesh {
	ChunkMesh mesh;
	const i32 stride = p_size + 2;

	const auto at = [&](i32 p_x, i32 p_y, i32 p_z) -> const MeshSample & {
		return p_samples[static_cast<size>(((p_x + 1) * stride + (p_y + 1)) * stride + (p_z + 1))];
	};

	for (i32 face = 0; face < 6; ++face) {
		const i32 axis = face / 2;
		const i32 sign = (face % 2 == 0) ? 1 : -1;
		const i32 axis_u = (axis + 1) % 3;
		const i32 axis_v = (axis + 2) % 3;

		std::vector<u32> keys(static_cast<size>(p_size) * p_size, 0);

		for (i32 slice = 0; slice < p_size; ++slice) {
			for (i32 vv = 0; vv < p_size; ++vv)
				for (i32 uu = 0; uu < p_size; ++uu) {
					std::array<i32, 3> cell{};
					cell[static_cast<size>(axis)] = slice;
					cell[static_cast<size>(axis_u)] = uu;
					cell[static_cast<size>(axis_v)] = vv;

					const MeshSample &sample = at(cell[0], cell[1], cell[2]);
					u32 key = 0;
					if (sample.solid) {
						std::array<i32, 3> neighbour = cell;
						neighbour[static_cast<size>(axis)] += sign;
						if (!at(neighbour[0], neighbour[1], neighbour[2]).solid)
							key = ((static_cast<u32>(sample.block_id) << 8) |
										  sample.brightness[static_cast<size>(face)]) +
									1;
					}
					keys[static_cast<size>(vv) * p_size + uu] = key;
				}

			for (i32 vv = 0; vv < p_size; ++vv) {
				for (i32 uu = 0; uu < p_size;) {
					const u32 key = keys[static_cast<size>(vv) * p_size + uu];
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
							p_block_scale, (key - 1) >> 8, static_cast<f32>((key - 1) & 0xffu) / 3.0f);

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

/// @brief Face `f` of the mesher (`+X -X +Y -Y +Z -Z`) → that cell's brightness.
inline auto face_brightness(const cellulose::HotCellAttribute &p_attribute, i32 p_face) -> u8 {
	switch (p_face) {
		case 0:
			return p_attribute.get_right_face_brightness();
		case 1:
			return p_attribute.get_left_face_brightness();
		case 2:
			return p_attribute.get_top_face_brightness();
		case 3:
			return p_attribute.get_bottom_face_brightness();
		case 4:
			return p_attribute.get_back_face_brightness();
		default:
			return p_attribute.get_front_face_brightness();
	}
}

/// @brief Fill an `(edge + 2)^3` apron grid for `p_chunk`. Shell cells only get
/// `solid`; interior cells also get `block_id` and the six brightnesses. Cells
/// are read as snapshots under each chunk's hot seqlock; absent chunks are empty.
template <typename WorldType, typename Predicate>
auto sample_chunk(WorldType &p_world, const ChunkPosition &p_chunk, i32 p_level, Predicate &p_is_solid) -> std::vector<MeshSample> {
	const i32 block = 1 << p_level;
	const i32 n = static_cast<i32>(chunk_edge_length) >> p_level;
	const i32 stride = n + 2;
	std::vector<MeshSample> samples(static_cast<size>(stride) * stride * stride);

	const i64 origin_x = static_cast<i64>(p_chunk.x) * static_cast<i64>(chunk_edge_length);
	const i64 origin_y = static_cast<i64>(p_chunk.y) * static_cast<i64>(chunk_edge_length);
	const i64 origin_z = static_cast<i64>(p_chunk.z) * static_cast<i64>(chunk_edge_length);

	const auto sample_cell = [&](const WorldPosition &p_cell, bool &p_solid, cellulose::HotCellAttribute &p_attribute) {
		const auto *chunk = p_world.find_chunk(to_chunk_position(p_cell));
		if (chunk == nullptr) {
			p_solid = false;
			return;
		}
		const LocalPosition local = to_local_position(p_cell);
		p_attribute = chunk->read_hot(
				[&](const auto &p_hot) { return p_hot[encode_cell_index(local)]; });
		p_solid = p_is_solid(p_attribute);
	};

	for (i32 x = -1; x <= n; ++x)
		for (i32 y = -1; y <= n; ++y)
			for (i32 z = -1; z <= n; ++z) {
				MeshSample &out = samples[static_cast<size>(((x + 1) * stride + (y + 1)) * stride + (z + 1))];
				const bool interior = x >= 0 && x < n && y >= 0 && y < n && z >= 0 && z < n;

				// A macro-cell is solid if any of its `block^3` cells is solid;
				// its attributes come from the first solid one found.
				bool found_solid = false;
				cellulose::HotCellAttribute representative{};
				for (i32 dx = 0; dx < block && !found_solid; ++dx)
					for (i32 dy = 0; dy < block && !found_solid; ++dy)
						for (i32 dz = 0; dz < block && !found_solid; ++dz) {
							const WorldPosition cell{
								origin_x + static_cast<i64>(x) * block + dx,
								origin_y + static_cast<i64>(y) * block + dy,
								origin_z + static_cast<i64>(z) * block + dz
							};
							bool solid = false;
							cellulose::HotCellAttribute attribute{};
							sample_cell(cell, solid, attribute);
							if (solid) {
								found_solid = true;
								representative = attribute;
							}
						}

				out.solid = found_solid;
				if (interior && found_solid) {
					out.block_id = representative.block_id;
					for (i32 face = 0; face < 6; ++face)
						out.brightness[static_cast<size>(face)] = face_brightness(representative, face);
				}
			}

	return samples;
}

} //namespace impl

/// @brief Greedy-mesh the chunk at `p_chunk`. `p_is_solid(HotCellAttribute)`
/// decides solidity; a face is emitted only where its neighbour (across chunk
/// boundaries; absent chunks count as empty) is not solid. Vertices are
/// chunk-local, in `[0, chunk_edge_length]`.
template <typename WorldType, typename Predicate>
auto mesh_chunk(WorldType &p_world, const ChunkPosition &p_chunk, Predicate &&p_is_solid) -> ChunkMesh {
	return greedy_mesh(
			impl::sample_chunk(p_world, p_chunk, 0, p_is_solid),
			static_cast<i32>(chunk_edge_length), 1.0f);
}

/// @brief As `mesh_chunk`, but merges each `(1 << p_level)^3` block of cells into
/// one macro-cell (solid if any cell is; attributes from the first solid cell).
/// `p_level` 0–5; `0` is exactly `mesh_chunk`. Vertices still span `[0, chunk_edge_length]`.
template <typename WorldType, typename Predicate>
auto mesh_chunk_lod(WorldType &p_world, const ChunkPosition &p_chunk, i32 p_level, Predicate &&p_is_solid) -> ChunkMesh {
	return greedy_mesh(
			impl::sample_chunk(p_world, p_chunk, p_level, p_is_solid),
			static_cast<i32>(chunk_edge_length) >> p_level,
			static_cast<f32>(1 << p_level));
}

} //namespace cellulose

#endif // CEL_MESH_HPP
