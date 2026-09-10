#include <doctest/doctest.h>

#include <cellulose/world.hpp>

#include <atomic>
#include <chrono>
#include <thread>

// The directory shared_mutex must let creates, lookups and iteration run
// concurrently without corrupting the table or handing back a stale pointer.
// Without the lock this races (and TSan flags it on Linux); with it, it is
// simply correct.
TEST_CASE("world directory tolerates concurrent create / lookup / iterate") {
	cellulose::World<> world;
	std::atomic<bool> stop{ false };

	std::thread creator([&] {
		cellulose::i32 i = 0;
		while (!stop.load(std::memory_order_acquire)) {
			world.chunk(cellulose::ChunkPosition{ i, 0, 0 });
			i = (i + 1) % 128;
		}
	});

	std::thread looker([&] {
		while (!stop.load(std::memory_order_acquire)) {
			auto *chunk = world.find_chunk(cellulose::ChunkPosition{ 7, 0, 0 });
			if (chunk != nullptr)
				chunk->hot_attribute(cellulose::LocalPosition{ 1, 1, 1 }).block_id = 3;
			(void)world.chunk_count();
			(void)world.has_chunk(cellulose::ChunkPosition{ 99, 0, 0 });
		}
	});

	std::thread walker([&] {
		while (!stop.load(std::memory_order_acquire)) {
			cellulose::size seen = 0;
			world.for_each_chunk(
					[&](const cellulose::ChunkPosition &, cellulose::Chunk<> &) { ++seen; });
		}
	});

	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	stop.store(true, std::memory_order_release);
	creator.join();
	looker.join();
	walker.join();

	CHECK(world.chunk_count() >= 1);
	CHECK(world.chunk_count() <= 128);
}
