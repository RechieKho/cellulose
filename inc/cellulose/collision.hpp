#ifndef CEL_COLLISION_HPP
#define CEL_COLLISION_HPP

#include "cell.hpp"
#include "chunk.hpp"
#include "coordinate.hpp"
#include "morton.hpp"
#include "types.hpp"
#include "vector.hpp"
#include "world.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace cellulose {

struct CollisionMove final {
	Vec3d position; //!< `p_box.min` after the (possibly shortened) move.
	Vec3i normal; //!< One component per blocked axis: `-1` / `+1` opposing the motion, `0` if that axis was free.
	bool collided;

	friend auto operator==(const CollisionMove &, const CollisionMove &) -> bool = default;
};

namespace impl {

template <typename WorldType, typename Predicate>
auto collision_cell_is_solid(WorldType &p_world, const WorldPosition &p_cell, Predicate &p_is_solid) -> bool {
	const auto *chunk = p_world.find_chunk(to_chunk_position(p_cell));
	if (chunk == nullptr)
		return false;
	const LocalPosition local = to_local_position(p_cell);
	const auto snapshot = chunk->read_hot(
			[&](const auto &p_hot) { return p_hot[encode_cell_index(local)]; });
	return p_is_solid(snapshot);
}

/// @brief Sweep `p_box` by `p_delta` along `p_axis` only. Returns the permitted
/// signed travel (clamped toward zero); sets `p_hit` when a solid stopped it.
template <typename WorldType, typename Predicate>
auto collision_sweep_axis(WorldType &p_world, const Aabb &p_box, size p_axis, f64 p_delta, Predicate &p_is_solid, bool &p_hit) -> f64 {
	p_hit = false;
	if (p_delta == 0.0)
		return 0.0;

	const size axis_b = (p_axis + 1) % 3;
	const size axis_c = (p_axis + 2) % 3;

	const auto cell_lo = [](f64 p_value) { return static_cast<i64>(std::floor(p_value)); };
	const auto cell_hi = [](f64 p_value) { return static_cast<i64>(std::ceil(p_value)) - 1; };

	const i64 a0 = cell_lo(std::min(p_box.min[p_axis], p_box.min[p_axis] + p_delta));
	const i64 a1 = cell_hi(std::max(p_box.max[p_axis], p_box.max[p_axis] + p_delta));
	const i64 b0 = cell_lo(p_box.min[axis_b]);
	const i64 b1 = cell_hi(p_box.max[axis_b]);
	const i64 c0 = cell_lo(p_box.min[axis_c]);
	const i64 c1 = cell_hi(p_box.max[axis_c]);

	bool found = false;
	f64 best = p_delta;

	for (i64 a = a0; a <= a1; ++a)
		for (i64 b = b0; b <= b1; ++b)
			for (i64 c = c0; c <= c1; ++c) {
				std::array<i64, 3> coordinate{};
				coordinate[p_axis] = a;
				coordinate[axis_b] = b;
				coordinate[axis_c] = c;
				const WorldPosition cell{ coordinate[0], coordinate[1], coordinate[2] };
				if (!collision_cell_is_solid(p_world, cell, p_is_solid))
					continue;

				const f64 allowed = p_delta > 0.0
						? static_cast<f64>(a) - p_box.max[p_axis]
						: static_cast<f64>(a + 1) - p_box.min[p_axis];

				if (!found) {
					best = allowed;
					found = true;
				} else {
					best = p_delta > 0.0 ? std::min(best, allowed) : std::max(best, allowed);
				}
			}

	if (!found)
		return p_delta;

	p_hit = true;
	return p_delta > 0.0 ? std::clamp(best, 0.0, p_delta) : std::clamp(best, p_delta, 0.0);
}

} //namespace impl

/// @brief Move the AABB `p_box` by `p_velocity` against solid voxels, resolving
/// one axis at a time (X, then Y, then Z — "collide and slide"). Returns the new
/// `p_box.min`, the blocked-axis normals, and whether anything was hit.
///
/// `p_is_solid(HotCellAttribute)` decides solidity; cells in absent chunks are
/// empty. Exact for axis-aligned motion of any length; only the diagonal
/// corner-order is an approximation.
template <typename WorldType, typename Predicate>
auto move_aabb(WorldType &p_world, const Aabb &p_box, const Vec3d &p_velocity, Predicate &&p_is_solid) -> CollisionMove {
	Aabb box = p_box;
	Vec3i normal{ 0, 0, 0 };
	bool collided = false;

	for (size axis = 0; axis < 3; ++axis) {
		bool hit = false;
		const f64 allowed = impl::collision_sweep_axis(p_world, box, axis, p_velocity[axis], p_is_solid, hit);
		box.min[axis] = box.min[axis] + allowed;
		box.max[axis] = box.max[axis] + allowed;
		if (hit) {
			collided = true;
			normal[axis] = p_velocity[axis] > 0.0 ? -1 : 1;
		}
	}

	return CollisionMove{ box.min, normal, collided };
}

} //namespace cellulose

#endif // CEL_COLLISION_HPP
