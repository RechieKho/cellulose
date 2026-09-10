#include <doctest/doctest.h>

#include <cellulose/atlas_builder.hpp>

#include <array>
#include <vector>

using cellulose::pack;
using cellulose::pack_grid;
using cellulose::PackedAtlas;
using cellulose::TextureID;
using cellulose::TileSize;
using cellulose::UvRect;

namespace {

auto overlaps(const UvRect &p_a, const UvRect &p_b) -> bool {
	return p_a.u0 < p_b.u1 && p_a.u1 > p_b.u0 && p_a.v0 < p_b.v1 && p_a.v1 > p_b.v0;
}

} //namespace

TEST_CASE("pack places every tile inside the sheet with no overlaps") {
	const std::array<TileSize, 5> tiles{
		TileSize{ 1, 16, 16 }, TileSize{ 2, 16, 16 }, TileSize{ 3, 32, 16 },
		TileSize{ 4, 16, 32 }, TileSize{ 5, 16, 16 }
	};
	const PackedAtlas packed = pack(tiles);

	CHECK(packed.width > 0);
	CHECK(packed.height > 0);

	std::vector<UvRect> rects;
	for (const auto &tile : tiles) {
		const UvRect r = packed.atlas.rect_of(tile.id);
		CHECK(r.u0 >= 0.0f);
		CHECK(r.v0 >= 0.0f);
		CHECK(r.u1 <= 1.0f + 1e-4f);
		CHECK(r.v1 <= 1.0f + 1e-4f);
		rects.push_back(r);
	}
	for (std::size_t i = 0; i < rects.size(); ++i)
		for (std::size_t j = i + 1; j < rects.size(); ++j)
			CHECK_FALSE(overlaps(rects[i], rects[j]));
}

TEST_CASE("pack of nothing yields a 1x1 sheet") {
	const PackedAtlas packed = pack(std::span<const TileSize>{});
	CHECK(packed.width == 1);
	CHECK(packed.height == 1);
}

TEST_CASE("pack_grid lays ids row-major in a near-square power-of-two grid") {
	const std::array<TextureID, 4> ids{ 1, 2, 3, 4 }; // count = max + 1 = 5
	const PackedAtlas packed = pack_grid(ids, 16);

	CHECK(packed.atlas.columns() == 4); // smallest pow2 with c*c >= 5
	CHECK(packed.atlas.rows() == 2);
	CHECK(packed.width == 64);
	CHECK(packed.height == 32);

	// id 1 -> cell (1, 0); id 4 -> cell (0, 1)
	const UvRect r1 = packed.atlas.rect_of(1);
	CHECK(r1.u0 == doctest::Approx(0.25f));
	CHECK(r1.v0 == doctest::Approx(0.0f));
	CHECK(r1.u1 == doctest::Approx(0.5f));
	CHECK(r1.v1 == doctest::Approx(0.5f));

	const UvRect r4 = packed.atlas.rect_of(4);
	CHECK(r4.u0 == doctest::Approx(0.0f));
	CHECK(r4.v0 == doctest::Approx(0.5f));

	// every tile is one grid cell, none overlap
	std::vector<UvRect> rects;
	for (TextureID id : ids)
		rects.push_back(packed.atlas.rect_of(id));
	for (std::size_t i = 0; i < rects.size(); ++i)
		for (std::size_t j = i + 1; j < rects.size(); ++j)
			CHECK_FALSE(overlaps(rects[i], rects[j]));
}

TEST_CASE("pack_grid of a single tile is 1x1") {
	const std::array<TextureID, 1> ids{ 0 };
	const PackedAtlas packed = pack_grid(ids, 16);
	CHECK(packed.width == 16);
	CHECK(packed.height == 16);
	CHECK(packed.atlas.columns() == 1);
}
