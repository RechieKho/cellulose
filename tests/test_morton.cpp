#include <doctest/doctest.h>

#include <cellulose/morton.hpp>

TEST_CASE("cell index round-trips for every position in a 32^3 chunk") {
	bool seen[32u * 32u * 32u] = {};

	for (cellulose::u8 x = 0; x < 32; ++x) {
		for (cellulose::u8 y = 0; y < 32; ++y) {
			for (cellulose::u8 z = 0; z < 32; ++z) {
				const cellulose::LocalPosition position{ x, y, z };
				const auto index = cellulose::encode_cell_index(position);

				REQUIRE(index < 32u * 32u * 32u);
				CHECK_FALSE(seen[index]); // encoding is a bijection
				seen[index] = true;

				CHECK(cellulose::decode_cell_index(index) == position);
			}
		}
	}
}
