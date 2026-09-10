#ifndef CEL_RAYCAST_HPP
#define CEL_RAYCAST_HPP

#include "cell.hpp"
#include "coordinate.hpp"
#include "morton.hpp"
#include "types.hpp"
#include "vector.hpp"
#include "world.hpp"
#include <limits>
#include <optional>
#include <tuple>

namespace cellulose {

struct Ray final {
	Vec3d origin;
	Vec3d direction;
};

struct RaycastHit final {
	WorldPosition cell;
	Vec3i normal; //!< The face the ray entered through; `{0,0,0}` if the ray began inside a solid.
	f64 distance;

	friend auto operator==(const RaycastHit &, const RaycastHit &) -> bool = default;
};

/// @brief March `p_ray` through the voxel grid (Amanatides & Woo DDA) until the
/// first cell for which `p_is_solid(HotCellAttribute)` holds, or `p_max_distance`
/// (measured along the normalised direction) is passed.
///
/// A cell in an absent chunk is treated as empty. If the ray starts inside a
/// solid cell that cell is returned with `distance == 0` and `normal == {0,0,0}`.
/// `p_is_solid` runs on a by-value snapshot taken under the chunk's hot seqlock.
template <typename WorldType, typename Predicate>
auto raycast(WorldType &p_world, const Ray &p_ray, f64 p_max_distance, Predicate &&p_is_solid)
		-> std::optional<RaycastHit> {
	const Vec3d direction = normalized(p_ray.direction);
	if (direction == Vec3d{ 0, 0, 0 })
		return std::nullopt;

	const auto solid_at = [&](const WorldPosition &p_cell) -> bool {
		const auto *chunk = p_world.find_chunk(to_chunk_position(p_cell));
		if (chunk == nullptr)
			return false;
		const LocalPosition local = to_local_position(p_cell);
		const auto snapshot = chunk->read_hot(
				[&](const auto &p_hot) { return p_hot[encode_cell_index(local)]; });
		return p_is_solid(snapshot);
	};

	WorldPosition cell = to_cell(p_ray.origin);
	if (solid_at(cell))
		return RaycastHit{ cell, Vec3i{ 0, 0, 0 }, 0.0 };

	const auto axis_setup = [](f64 p_origin, i64 p_cell, f64 p_direction) {
		constexpr f64 infinity = std::numeric_limits<f64>::infinity();
		if (p_direction > 0.0)
			return std::tuple<i64, f64, f64>{
				1, (static_cast<f64>(p_cell) + 1.0 - p_origin) / p_direction, 1.0 / p_direction
			};
		if (p_direction < 0.0)
			return std::tuple<i64, f64, f64>{
				-1, (static_cast<f64>(p_cell) - p_origin) / p_direction, -1.0 / p_direction
			};
		return std::tuple<i64, f64, f64>{ 0, infinity, infinity };
	};

	auto [step_x, t_max_x, t_delta_x] = axis_setup(p_ray.origin.x, cell.x, direction.x);
	auto [step_y, t_max_y, t_delta_y] = axis_setup(p_ray.origin.y, cell.y, direction.y);
	auto [step_z, t_max_z, t_delta_z] = axis_setup(p_ray.origin.z, cell.z, direction.z);

	f64 distance = 0.0;

	while (distance <= p_max_distance) {
		Vec3i normal{ 0, 0, 0 };

		if (t_max_x <= t_max_y && t_max_x <= t_max_z) {
			cell.x += step_x;
			distance = t_max_x;
			t_max_x += t_delta_x;
			normal.x = static_cast<i32>(-step_x);
		} else if (t_max_y <= t_max_z) {
			cell.y += step_y;
			distance = t_max_y;
			t_max_y += t_delta_y;
			normal.y = static_cast<i32>(-step_y);
		} else {
			cell.z += step_z;
			distance = t_max_z;
			t_max_z += t_delta_z;
			normal.z = static_cast<i32>(-step_z);
		}

		if (distance > p_max_distance)
			return std::nullopt;

		if (solid_at(cell))
			return RaycastHit{ cell, normal, distance };
	}

	return std::nullopt;
}

} //namespace cellulose

#endif // CEL_RAYCAST_HPP
