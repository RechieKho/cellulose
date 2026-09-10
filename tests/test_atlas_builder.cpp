#include <doctest/doctest.h>

#include <cellulose/atlas_builder.hpp>

#include <array>
#include <vector>

using cellulose::pack;
using cellulose::PackedAtlas;
using cellulose::strip;
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

TEST_CASE("strip is a single column with one row per id, sized to max(id) + 1") {
	const std::array<TextureID, 3> ids{ 1, 2, 3 };
	const PackedAtlas packed = strip(ids, 16);
	CHECK(packed.width == 16);
	CHECK(packed.height == 64); // rows 0..3, id 0 unused

	// row id spans v in [id/4, (id+1)/4], full width
	const UvRect r1 = packed.atlas.rect_of(1);
	CHECK(r1.u0 == doctest::Approx(0.0f));
	CHECK(r1.u1 == doctest::Approx(1.0f));
	CHECK(r1.v0 == doctest::Approx(0.25f));
	CHECK(r1.v1 == doctest::Approx(0.5f));

	const UvRect r3 = packed.atlas.rect_of(3);
	CHECK(r3.v0 == doctest::Approx(0.75f));
	CHECK(r3.v1 == doctest::Approx(1.0f));
}
