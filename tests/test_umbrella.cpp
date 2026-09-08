#include <doctest/doctest.h>

#include <cellulose/cellulose.hpp>

TEST_CASE("umbrella header pulls in World and Chunk") {
	cellulose::World<> world;
	world.chunk(cellulose::ChunkPosition{ 0, 0, 0 }).fill_hot(cellulose::HotCellAttribute{ 1, 0 });
	CHECK(world.find_hot_attribute(cellulose::WorldPosition{ 0, 0, 0 })->block_id == 1);
}

TEST_CASE("const World resolves hot attributes through the const overloads") {
	cellulose::World<> world;
	world.chunk(cellulose::ChunkPosition{ 0, 0, 0 }).fill_hot(cellulose::HotCellAttribute{ 3, 0 });

	const cellulose::World<> &const_world = world;
	CHECK(const_world.has_chunk(cellulose::ChunkPosition{ 0, 0, 0 }));
	CHECK(const_world.find_chunk(cellulose::ChunkPosition{ 0, 0, 0 }) != nullptr);
	const auto *attribute = const_world.find_hot_attribute(cellulose::WorldPosition{ 0, 0, 0 });
	REQUIRE(attribute != nullptr);
	CHECK(attribute->block_id == 3);
}
