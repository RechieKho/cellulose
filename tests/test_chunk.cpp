#include <doctest/doctest.h>

#include <cellulose/chunk.hpp>

#include <atomic>
#include <chrono>
#include <thread>

static_assert(alignof(cellulose::Chunk<>) >= cellulose::cache_line_size);

TEST_CASE("chunk edge length and cell count are consistent") {
	CHECK(cellulose::chunk_cell_count == cellulose::chunk_edge_length * cellulose::chunk_edge_length * cellulose::chunk_edge_length);
	CHECK(cellulose::chunk_cell_count == 32768u);
}

TEST_CASE("hot attributes are addressable by local position and by cell index") {
	cellulose::Chunk<> chunk;

	const cellulose::LocalPosition position{ 5, 6, 7 };
	chunk.hot_attribute(position).block_id = 9;

	const auto index = cellulose::encode_cell_index(position);
	CHECK(chunk.hot_attribute(index).block_id == 9);

	// A different cell is unaffected.
	CHECK(chunk.hot_attribute(cellulose::LocalPosition{ 7, 6, 5 }).block_id == 0);
}

TEST_CASE("fill_hot writes every cell") {
	cellulose::Chunk<> chunk;
	cellulose::HotCellAttribute stone{ 42, 0 };
	chunk.fill_hot(stone);

	CHECK(chunk.hot_attribute(cellulose::LocalPosition{ 0, 0, 0 }).block_id == 42);
	CHECK(chunk.hot_attribute(cellulose::LocalPosition{ 31, 31, 31 }).block_id == 42);
}

TEST_CASE("hot attributes are writable by cell index and readable through a const chunk") {
	cellulose::Chunk<> chunk;

	const cellulose::LocalPosition position{ 12, 3, 30 };
	const auto index = cellulose::encode_cell_index(position);
	chunk.hot_attribute(index).block_id = 21;

	const cellulose::Chunk<> &const_chunk = chunk;
	CHECK(const_chunk.hot_attribute(index).block_id == 21);
	CHECK(const_chunk.hot_attribute(position).block_id == 21);
	CHECK(const_chunk.hot_attribute(cellulose::LocalPosition{ 0, 0, 0 }).block_id == 0);
}

TEST_CASE("chunk exposes its packed and sparse collections") {
	cellulose::Chunk<
			cellulose::HotCellAttribute,
			cellulose::PackedChunkAttributes<cellulose::u16>,
			cellulose::SparseChunkAttributes<std::array<cellulose::u8, 16>>>
			chunk;

	chunk.packed().get<cellulose::u16>()[3] = 7;
	CHECK(chunk.packed().get<cellulose::u16>()[3] == 7);
	CHECK(chunk.sparse().get<std::array<cellulose::u8, 16>>().empty());
}

TEST_CASE("functor accessors round-trip each tier under its lock") {
	cellulose::Chunk<
			cellulose::HotCellAttribute,
			cellulose::PackedChunkAttributes<cellulose::u16>,
			cellulose::SparseChunkAttributes<std::array<cellulose::u8, 16>>>
			chunk;

	const auto index = cellulose::encode_cell_index(cellulose::LocalPosition{ 4, 5, 6 });

	chunk.write_hot([&](auto &hot) { hot[index] = cellulose::HotCellAttribute{ 12, 0 }; });
	const auto block_id = chunk.read_hot([&](const auto &hot) { return hot[index].block_id; });
	CHECK(block_id == 12);

	chunk.write_cold([&](auto &packed) { packed.template get<cellulose::u16>()[index] = 9; });
	const auto cold = chunk.read_cold([&](const auto &packed) {
		return packed.template get<cellulose::u16>()[index];
	});
	CHECK(cold == 9);

	chunk.write_sparse([&](auto &sparse) {
		sparse.template get<std::array<cellulose::u8, 16>>()[index] = std::array<cellulose::u8, 16>{};
	});
	const auto present = chunk.read_sparse([&](const auto &sparse) {
		return sparse.template get<std::array<cellulose::u8, 16>>().contains(index);
	});
	CHECK(present);
}

TEST_CASE("revision advances on every write and not on reads") {
	cellulose::Chunk<> chunk;
	CHECK(chunk.revision() == 0);

	chunk.write_hot([](auto &hot) { hot[0] = cellulose::HotCellAttribute{ 1, 0 }; });
	const auto after_write = chunk.revision();
	CHECK(after_write > 0);

	(void)chunk.read_hot([](const auto &hot) { return hot[0].block_id; });
	CHECK(chunk.revision() == after_write);

	chunk.write_hot([](auto &) {}); // even a no-op write bumps it (conservative)
	CHECK(chunk.revision() > after_write);
}

// The writer keeps block_id == state; a torn read (seen without the seqlock)
// would pair fields from two different writes. Not applicable under
// CELLULOSE_STRICT_ATOMICS, where the write view only permits whole-element
// stores (and so cannot tear).
#ifndef CELLULOSE_STRICT_ATOMICS
TEST_CASE("concurrent write_hot / read_hot never yields a torn HotCellAttribute") {
	cellulose::Chunk<> chunk;
	const auto index = cellulose::encode_cell_index(cellulose::LocalPosition{ 10, 20, 30 });

	std::atomic<bool> stop{ false };
	std::atomic<cellulose::u64> tears{ 0 };

	std::thread writer([&] {
		cellulose::u16 v = 1;
		while (!stop.load(std::memory_order_acquire)) {
			chunk.write_hot([&](auto &hot) {
				hot[index].block_id = v;
				hot[index].state = v;
			});
			if (++v == 0)
				v = 1;
		}
	});

	const auto run_reader = [&] {
		while (!stop.load(std::memory_order_acquire)) {
			const auto cell = chunk.read_hot([&](const auto &hot) { return hot[index]; });
			if (cell.block_id != cell.state)
				++tears;
		}
	};
	std::thread reader_a(run_reader);
	std::thread reader_b(run_reader);

	std::this_thread::sleep_for(std::chrono::milliseconds(150));
	stop.store(true, std::memory_order_release);
	writer.join();
	reader_a.join();
	reader_b.join();

	CHECK(tears.load() == 0);
}
#endif // CELLULOSE_STRICT_ATOMICS
