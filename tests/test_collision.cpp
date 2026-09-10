#include <doctest/doctest.h>

#include <cellulose/collision.hpp>

namespace {

auto solid_predicate() {
	return [](const cellulose::HotCellAttribute &p_attribute) { return p_attribute.block_id != 0; };
}

auto set_solid(cellulose::World<> &p_world, const cellulose::WorldPosition &p_cell) -> void {
	p_world.chunk(cellulose::to_chunk_position(p_cell))
			.hot_attribute(cellulose::to_local_position(p_cell))
			.block_id = 1;
}

const cellulose::Aabb unit_box{ { 0.0, 0.0, 0.0 }, { 1.0, 1.0, 1.0 } };

} //namespace

TEST_CASE("a box moving into a wall stops flush against it") {
	cellulose::World<> world;
	set_solid(world, { 5, 0, 0 });

	const auto move = cellulose::move_aabb(
			world, unit_box, cellulose::Vec3d{ 5.0, 0.0, 0.0 }, solid_predicate());

	CHECK(move.collided);
	CHECK(move.normal == cellulose::Vec3i{ -1, 0, 0 });
	CHECK(move.position.x == doctest::Approx(4.0)); // box.max.x flush at 5
}

TEST_CASE("a box driven into an inside corner slides to the corner on both axes") {
	cellulose::World<> world;
	for (cellulose::i64 i = 0; i <= 5; ++i) {
		set_solid(world, { 5, i, 0 }); // wall along x = 5
		set_solid(world, { i, 5, 0 }); // wall along y = 5
	}

	const auto move = cellulose::move_aabb(
			world, unit_box, cellulose::Vec3d{ 5.0, 5.0, 0.0 }, solid_predicate());

	CHECK(move.collided);
	CHECK(move.normal == cellulose::Vec3i{ -1, -1, 0 });
	CHECK(move.position.x == doctest::Approx(4.0));
	CHECK(move.position.y == doctest::Approx(4.0));
}

TEST_CASE("a box moving through open space travels its whole velocity") {
	cellulose::World<> world;
	world.chunk({ 0, 0, 0 });

	const auto move = cellulose::move_aabb(
			world, unit_box, cellulose::Vec3d{ 2.0, -1.0, 0.5 }, solid_predicate());

	CHECK_FALSE(move.collided);
	CHECK(move.normal == cellulose::Vec3i{ 0, 0, 0 });
	CHECK(move.position.x == doctest::Approx(2.0));
	CHECK(move.position.y == doctest::Approx(-1.0));
	CHECK(move.position.z == doctest::Approx(0.5));
}

TEST_CASE("a falling box lands on top of the floor") {
	cellulose::World<> world;
	set_solid(world, { 0, 0, 0 }); // floor cell spans y in [0, 1)

	const cellulose::Aabb box{ { 0.0, 5.0, 0.0 }, { 1.0, 6.0, 1.0 } };
	const auto move = cellulose::move_aabb(
			world, box, cellulose::Vec3d{ 0.0, -10.0, 0.0 }, solid_predicate());

	CHECK(move.collided);
	CHECK(move.normal == cellulose::Vec3i{ 0, 1, 0 });
	CHECK(move.position.y == doctest::Approx(1.0)); // resting on the floor's top face
}
