#include <doctest/doctest.h>

#include <cellulose/coordinate.hpp>

using cellulose::ChunkPosition;
using cellulose::LocalPosition;
using cellulose::WorldPosition;

TEST_CASE("world position splits into chunk + local for the positive octant") {
	const WorldPosition world{ 35, 2, 63 };
	CHECK(cellulose::to_chunk_position(world) == ChunkPosition{ 1, 0, 1 });
	CHECK(cellulose::to_local_position(world) == LocalPosition{ 3, 2, 31 });
}

TEST_CASE("world position splits correctly for negative coordinates") {
	const WorldPosition world{ -1, -32, -33 };
	CHECK(cellulose::to_chunk_position(world) == ChunkPosition{ -1, -1, -2 });
	CHECK(cellulose::to_local_position(world) == LocalPosition{ 31, 0, 31 });
}

TEST_CASE("chunk + local recombine into the original world position") {
	const WorldPosition world{ -33, 100, 7 };
	const auto chunk = cellulose::to_chunk_position(world);
	const auto local = cellulose::to_local_position(world);
	CHECK(cellulose::to_world_position(chunk, local) == world);
}

TEST_CASE("ChunkPositionHash is deterministic and distinguishes neighbours") {
	const cellulose::ChunkPositionHash hash;
	CHECK(hash(ChunkPosition{ 1, 2, 3 }) == hash(ChunkPosition{ 1, 2, 3 }));
	CHECK(hash(ChunkPosition{ 1, 2, 3 }) != hash(ChunkPosition{ 1, 2, 4 }));
}
