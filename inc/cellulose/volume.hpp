#ifndef CEL_VOLUME_HPP
#define CEL_VOLUME_HPP

#include "cell.hpp"
#include "chunk.hpp"
#include "coordinate.hpp"
#include "morton.hpp"
#include "types.hpp"
#include "vector.hpp"
#include "world.hpp"
#include <algorithm>

namespace cellulose {

/// @brief Call `p_visitor(const WorldPosition &, const HotCellAttribute &)` for
/// every **existing** cell whose unit voxel overlaps `p_box`. Iteration is
/// chunk-major: absent chunks are skipped. Each cell is a by-value snapshot taken
/// under the chunk's hot seqlock; the visitor runs outside that lock.
template <typename WorldType, typename Visitor>
auto for_each_cell_in_aabb(WorldType &p_world, const Aabb &p_box, Visitor &&p_visitor) -> void {
	const WorldPosition lo = to_cell(p_box.min);
	const WorldPosition hi = to_cell(p_box.max);
	if (hi.x < lo.x || hi.y < lo.y || hi.z < lo.z)
		return;

	const ChunkPosition chunk_lo = to_chunk_position(lo);
	const ChunkPosition chunk_hi = to_chunk_position(hi);
	constexpr i64 edge = static_cast<i64>(chunk_edge_length);

	for (i32 cx = chunk_lo.x; cx <= chunk_hi.x; ++cx)
		for (i32 cy = chunk_lo.y; cy <= chunk_hi.y; ++cy)
			for (i32 cz = chunk_lo.z; cz <= chunk_hi.z; ++cz) {
				auto *chunk = p_world.find_chunk(ChunkPosition{ cx, cy, cz });
				if (chunk == nullptr)
					continue;

				const i64 base_x = static_cast<i64>(cx) * edge;
				const i64 base_y = static_cast<i64>(cy) * edge;
				const i64 base_z = static_cast<i64>(cz) * edge;

				const i64 x0 = std::max<i64>(lo.x, base_x);
				const i64 x1 = std::min<i64>(hi.x, base_x + edge - 1);
				const i64 y0 = std::max<i64>(lo.y, base_y);
				const i64 y1 = std::min<i64>(hi.y, base_y + edge - 1);
				const i64 z0 = std::max<i64>(lo.z, base_z);
				const i64 z1 = std::min<i64>(hi.z, base_z + edge - 1);

				for (i64 wx = x0; wx <= x1; ++wx)
					for (i64 wy = y0; wy <= y1; ++wy)
						for (i64 wz = z0; wz <= z1; ++wz) {
							const WorldPosition cell{ wx, wy, wz };
							const LocalPosition local = to_local_position(cell);
							const auto snapshot = chunk->read_hot(
									[&](const auto &p_hot) { return p_hot[encode_cell_index(local)]; });
							p_visitor(cell, snapshot);
						}
			}
}

/// @brief As `for_each_cell_in_aabb`, restricted to cells whose unit voxel comes
/// within `p_radius` of `p_center` (closest-point test — no corner clipping).
template <typename WorldType, typename Visitor>
auto for_each_cell_in_sphere(WorldType &p_world, const Vec3d &p_center, f64 p_radius, Visitor &&p_visitor) -> void {
	if (p_radius < 0.0)
		return;

	const Vec3d extent{ p_radius, p_radius, p_radius };
	const Aabb bounds{ p_center - extent, p_center + extent };
	const f64 radius_squared = p_radius * p_radius;

	for_each_cell_in_aabb(
			p_world, bounds,
			[&](const WorldPosition &p_cell, const HotCellAttribute &p_attribute) {
				const Vec3d min = to_point(p_cell);
				const Vec3d max = min + Vec3d{ 1.0, 1.0, 1.0 };
				const Vec3d closest{
					std::clamp(p_center.x, min.x, max.x),
					std::clamp(p_center.y, min.y, max.y),
					std::clamp(p_center.z, min.z, max.z)
				};
				if (length_squared(closest - p_center) <= radius_squared)
					p_visitor(p_cell, p_attribute);
			});
}

} //namespace cellulose

#endif // CEL_VOLUME_HPP
