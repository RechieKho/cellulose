#include <doctest/doctest.h>

#include <cellulose/cell.hpp>

TEST_CASE("PackedCellAttributeCollection::get returns a mutable reference") {
	cellulose::PackedCellAttributeCollection<4, cellulose::u32> collection;
	collection.get<cellulose::u32>()[2] = 42u;
	CHECK(collection.get<cellulose::u32>()[2] == 42u);
}

TEST_CASE("SparseCellAttributeCollection::get returns a mutable reference") {
	cellulose::SparseCellAttributeCollection<std::array<cellulose::u8, 16>> collection;
	collection.get<std::array<cellulose::u8, 16>>()[7] = std::array<cellulose::u8, 16>{};
	CHECK(collection.get<std::array<cellulose::u8, 16>>().contains(7));
}
