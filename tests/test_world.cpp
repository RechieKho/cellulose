#include <doctest/doctest.h>

#include <cellulose/world.hpp>

using cellulose::ChunkPosition;
using cellulose::WorldPosition;

TEST_CASE("a fresh world has no chunks") {
	cellulose::World<> world;
	CHECK(world.chunk_count() == 0);
	CHECK_FALSE(world.has_chunk(ChunkPosition{ 0, 0, 0 }));
	CHECK(world.find_chunk(ChunkPosition{ 0, 0, 0 }) == nullptr);
}

TEST_CASE("chunk() creates on first access and is idempotent afterwards") {
	cellulose::World<> world;

	auto &created = world.chunk(ChunkPosition{ 1, -2, 3 });
	CHECK(world.chunk_count() == 1);
	CHECK(world.has_chunk(ChunkPosition{ 1, -2, 3 }));

	created.hot_attribute(cellulose::LocalPosition{ 0, 0, 0 }).block_id = 5;
	CHECK(&world.chunk(ChunkPosition{ 1, -2, 3 }) == &created);
	CHECK(world.chunk_count() == 1);
	CHECK(world.find_chunk(ChunkPosition{ 1, -2, 3 })->hot_attribute(cellulose::LocalPosition{ 0, 0, 0 }).block_id == 5);
}

TEST_CASE("remove_chunk erases and reports whether anything was removed") {
	cellulose::World<> world;
	world.chunk(ChunkPosition{ 0, 0, 0 });

	CHECK(world.remove_chunk(ChunkPosition{ 0, 0, 0 }));
	CHECK(world.chunk_count() == 0);
	CHECK_FALSE(world.remove_chunk(ChunkPosition{ 0, 0, 0 }));
}

TEST_CASE("for_each_chunk visits every chunk once") {
	cellulose::World<> world;
	world.chunk(ChunkPosition{ 0, 0, 0 });
	world.chunk(ChunkPosition{ 1, 0, 0 });
	world.chunk(ChunkPosition{ 0, 1, 0 });

	cellulose::size visited = 0;
	world.for_each_chunk([&](const ChunkPosition &, cellulose::Chunk<> &) { ++visited; });
	CHECK(visited == 3);
}

TEST_CASE("find_hot_attribute resolves a world position through its chunk") {
	cellulose::World<> world;

	CHECK(world.find_hot_attribute(WorldPosition{ -1, 40, 5 }) == nullptr);

	world.chunk(cellulose::to_chunk_position(WorldPosition{ -1, 40, 5 }))
			.hot_attribute(cellulose::to_local_position(WorldPosition{ -1, 40, 5 }))
			.block_id = 77;

	auto *attribute = world.find_hot_attribute(WorldPosition{ -1, 40, 5 });
	REQUIRE(attribute != nullptr);
	CHECK(attribute->block_id == 77);

	// A neighbouring world position in the same chunk is still default.
	CHECK(world.find_hot_attribute(WorldPosition{ -2, 40, 5 })->block_id == 0);
}
