#include <doctest/doctest.h>

#include <cellulose/vector.hpp>

using cellulose::Aabb;
using cellulose::Vec3d;
using cellulose::WorldPosition;

TEST_CASE("vector arithmetic and products") {
	const Vec3d a{ 1.0, 2.0, 3.0 };
	const Vec3d b{ 4.0, 5.0, 6.0 };

	CHECK((a + b) == Vec3d{ 5.0, 7.0, 9.0 });
	CHECK((b - a) == Vec3d{ 3.0, 3.0, 3.0 });
	CHECK((a * 2.0) == Vec3d{ 2.0, 4.0, 6.0 });
	CHECK((-a) == Vec3d{ -1.0, -2.0, -3.0 });
	CHECK(cellulose::dot(a, b) == doctest::Approx(32.0));
	CHECK(cellulose::cross(Vec3d{ 1, 0, 0 }, Vec3d{ 0, 1, 0 }) == Vec3d{ 0, 0, 1 });
	CHECK(cellulose::length_squared(Vec3d{ 2, 3, 6 }) == doctest::Approx(49.0));

	Vec3d indexed{ 7.0, 8.0, 9.0 };
	CHECK(indexed[0] == 7.0);
	CHECK(indexed[2] == 9.0);
	indexed[1] = 0.0;
	CHECK(indexed == Vec3d{ 7.0, 0.0, 9.0 });
}

TEST_CASE("normalized has unit length, and the zero vector stays zero") {
	const auto unit = cellulose::normalized(Vec3d{ 3.0, 4.0, 0.0 });
	CHECK(cellulose::length(unit) == doctest::Approx(1.0));
	CHECK(unit.x == doctest::Approx(0.6));
	CHECK(unit.y == doctest::Approx(0.8));
	CHECK(cellulose::normalized(Vec3d{ 0, 0, 0 }) == Vec3d{ 0, 0, 0 });
}

TEST_CASE("to_cell floors toward negative infinity and to_point is its corner") {
	CHECK(cellulose::to_cell(Vec3d{ -0.1, 0.0, 2.9 }) == WorldPosition{ -1, 0, 2 });
	CHECK(cellulose::to_cell(Vec3d{ 31.999, -32.0, 5.0 }) == WorldPosition{ 31, -32, 5 });
	CHECK(cellulose::to_point(WorldPosition{ -1, 0, 2 }) == Vec3d{ -1.0, 0.0, 2.0 });
}

TEST_CASE("aabb contains and intersects") {
	const Aabb box{ { 0, 0, 0 }, { 2, 2, 2 } };

	CHECK(box.contains(Vec3d{ 1, 1, 1 }));
	CHECK_FALSE(box.contains(Vec3d{ 3, 1, 1 }));
	CHECK(box.center() == Vec3d{ 1, 1, 1 });

	CHECK(box.intersects(Aabb{ { 1, 1, 1 }, { 3, 3, 3 } }));
	CHECK(box.intersects(Aabb{ { 2, 2, 2 }, { 4, 4, 4 } })); // touching counts
	CHECK_FALSE(box.intersects(Aabb{ { 3, 0, 0 }, { 4, 1, 1 } }));
}
