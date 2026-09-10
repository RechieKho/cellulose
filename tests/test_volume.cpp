#include <doctest/doctest.h>

#include <cellulose/volume.hpp>

#include <algorithm>
#include <vector>

namespace {

auto touch_chunk(cellulose::World<> &p_world, const cellulose::ChunkPosition &p_chunk) -> void {
	p_world.chunk(p_chunk);
}

auto collect_aabb(cellulose::World<> &p_world, const cellulose::Aabb &p_box)
		-> std::vector<cellulose::WorldPosition> {
	std::vector<cellulose::WorldPosition> cells;
	cellulose::for_each_cell_in_aabb(
			p_world, p_box,
			[&](const cellulose::WorldPosition &p_cell, const cellulose::HotCellAttribute &) {
				cells.push_back(p_cell);
			});
	return cells;
}

} //namespace

TEST_CASE("for_each_cell_in_aabb visits every cell of a 2x2x2 span once") {
	cellulose::World<> world;
	touch_chunk(world, { 0, 0, 0 });

	const auto cells = collect_aabb(world, cellulose::Aabb{ { 5.0, 5.0, 5.0 }, { 6.5, 6.5, 6.5 } });

	CHECK(cells.size() == 8);
	for (cellulose::i64 x = 5; x <= 6; ++x)
		for (cellulose::i64 y = 5; y <= 6; ++y)
			for (cellulose::i64 z = 5; z <= 6; ++z)
				CHECK(std::find(cells.begin(), cells.end(), cellulose::WorldPosition{ x, y, z }) != cells.end());
}

TEST_CASE("for_each_cell_in_aabb spans chunk boundaries and skips absent chunks") {
	cellulose::World<> world;
	touch_chunk(world, { 0, 0, 0 });

	const cellulose::Aabb box{ { 30.0, 0.0, 0.0 }, { 33.5, 0.5, 0.5 } };

	// Only chunk (0,0,0) exists: cells 30, 31 are visited; 32, 33 (chunk 1) are not.
	auto cells = collect_aabb(world, box);
	CHECK(cells.size() == 2);
	CHECK(std::find(cells.begin(), cells.end(), cellulose::WorldPosition{ 31, 0, 0 }) != cells.end());

	touch_chunk(world, { 1, 0, 0 });
	cells = collect_aabb(world, box);
	CHECK(cells.size() == 4);
	CHECK(std::find(cells.begin(), cells.end(), cellulose::WorldPosition{ 32, 0, 0 }) != cells.end());
}

TEST_CASE("for_each_cell_in_sphere includes the centre cell and its 6 face neighbours only") {
	cellulose::World<> world;
	touch_chunk(world, { 0, 0, 0 });

	std::vector<cellulose::WorldPosition> cells;
	cellulose::for_each_cell_in_sphere(
			world, cellulose::Vec3d{ 10.5, 10.5, 10.5 }, 0.6,
			[&](const cellulose::WorldPosition &p_cell, const cellulose::HotCellAttribute &) {
				cells.push_back(p_cell);
			});

	CHECK(cells.size() == 7);
	const cellulose::WorldPosition expected[] = {
		{ 10, 10, 10 },
		{ 9, 10, 10 }, { 11, 10, 10 },
		{ 10, 9, 10 }, { 10, 11, 10 },
		{ 10, 10, 9 }, { 10, 10, 11 }
	};
	for (const auto &cell : expected)
		CHECK(std::find(cells.begin(), cells.end(), cell) != cells.end());
}
