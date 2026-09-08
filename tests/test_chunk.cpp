#include <doctest/doctest.h>

#include <cellulose/chunk.hpp>

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

TEST_CASE("chunk exposes its packed and sparse collections") {
	cellulose::Chunk<
			cellulose::PackedCellAttributeCollection<cellulose::chunk_cell_count, cellulose::u16>,
			cellulose::SparseCellAttributeCollection<std::array<cellulose::u8, 16>>>
			chunk;

	chunk.packed().get<cellulose::u16>()[3] = 7;
	CHECK(chunk.packed().get<cellulose::u16>()[3] == 7);
	CHECK(chunk.sparse().get<std::array<cellulose::u8, 16>>().empty());
}
