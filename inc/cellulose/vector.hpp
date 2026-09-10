#ifndef CEL_VECTOR_HPP
#define CEL_VECTOR_HPP

#include "coordinate.hpp"
#include "types.hpp"
#include <algorithm>
#include <cmath>

namespace cellulose {

/// @brief A 3-component vector. `Vec3` / `Vec3d` for real coordinates and
/// directions, `Vec3i` for exact quantities like face normals.
template <typename T>
struct Vector3 final {
	T x;
	T y;
	T z;

	friend auto operator==(const Vector3 &, const Vector3 &) -> bool = default;

	/// @brief Axis access: `0` → x, `1` → y, `2` → z.
	auto operator[](size p_axis) -> T & {
		switch (p_axis) {
			case 0:
				return x;
			case 1:
				return y;
			default:
				return z;
		}
	}

	auto operator[](size p_axis) const -> const T & {
		switch (p_axis) {
			case 0:
				return x;
			case 1:
				return y;
			default:
				return z;
		}
	}

	auto operator+(const Vector3 &p_other) const -> Vector3 {
		return { x + p_other.x, y + p_other.y, z + p_other.z };
	}

	auto operator-(const Vector3 &p_other) const -> Vector3 {
		return { x - p_other.x, y - p_other.y, z - p_other.z };
	}

	auto operator-() const -> Vector3 { return { -x, -y, -z }; }

	auto operator*(T p_scalar) const -> Vector3 {
		return { x * p_scalar, y * p_scalar, z * p_scalar };
	}

	auto operator/(T p_scalar) const -> Vector3 {
		return { x / p_scalar, y / p_scalar, z / p_scalar };
	}
};

using Vec3 = Vector3<f32>;
using Vec3d = Vector3<f64>;
using Vec3i = Vector3<i32>;

template <typename T>
auto dot(const Vector3<T> &p_a, const Vector3<T> &p_b) -> T {
	return p_a.x * p_b.x + p_a.y * p_b.y + p_a.z * p_b.z;
}

template <typename T>
auto cross(const Vector3<T> &p_a, const Vector3<T> &p_b) -> Vector3<T> {
	return {
		p_a.y * p_b.z - p_a.z * p_b.y,
		p_a.z * p_b.x - p_a.x * p_b.z,
		p_a.x * p_b.y - p_a.y * p_b.x
	};
}

template <typename T>
auto length_squared(const Vector3<T> &p_vector) -> T {
	return dot(p_vector, p_vector);
}

template <typename T>
auto length(const Vector3<T> &p_vector) -> T {
	return static_cast<T>(std::sqrt(static_cast<f64>(length_squared(p_vector))));
}

template <typename T>
auto normalized(const Vector3<T> &p_vector) -> Vector3<T> {
	const auto magnitude = length(p_vector);
	return magnitude == T{ 0 } ? Vector3<T>{ 0, 0, 0 } : p_vector / magnitude;
}

template <typename T>
auto component_min(const Vector3<T> &p_a, const Vector3<T> &p_b) -> Vector3<T> {
	return { std::min(p_a.x, p_b.x), std::min(p_a.y, p_b.y), std::min(p_a.z, p_b.z) };
}

template <typename T>
auto component_max(const Vector3<T> &p_a, const Vector3<T> &p_b) -> Vector3<T> {
	return { std::max(p_a.x, p_b.x), std::max(p_a.y, p_b.y), std::max(p_a.z, p_b.z) };
}

/// @brief Axis-aligned bounding box in world (block) space.
struct Aabb final {
	Vec3d min;
	Vec3d max;

	friend auto operator==(const Aabb &, const Aabb &) -> bool = default;

	auto center() const -> Vec3d { return (min + max) * 0.5; }

	auto contains(const Vec3d &p_point) const -> bool {
		return p_point.x >= min.x && p_point.x <= max.x &&
				p_point.y >= min.y && p_point.y <= max.y &&
				p_point.z >= min.z && p_point.z <= max.z;
	}

	auto intersects(const Aabb &p_other) const -> bool {
		return min.x <= p_other.max.x && max.x >= p_other.min.x &&
				min.y <= p_other.max.y && max.y >= p_other.min.y &&
				min.z <= p_other.max.z && max.z >= p_other.min.z;
	}
};

/// @brief The cell (unit voxel) that contains a world-space point.
inline auto to_cell(const Vec3d &p_point) -> WorldPosition {
	return {
		static_cast<i64>(std::floor(p_point.x)),
		static_cast<i64>(std::floor(p_point.y)),
		static_cast<i64>(std::floor(p_point.z))
	};
}

/// @brief The minimum (corner) world-space point of a cell.
inline auto to_point(const WorldPosition &p_cell) -> Vec3d {
	return {
		static_cast<f64>(p_cell.x),
		static_cast<f64>(p_cell.y),
		static_cast<f64>(p_cell.z)
	};
}

} //namespace cellulose

#endif // CEL_VECTOR_HPP
