#include <doctest/doctest.h>

#include <cellulose/texture.hpp>

#include <cellulose/block.hpp>

using cellulose::FaceTextures;
using cellulose::TextureAtlas;
using cellulose::TextureID;
using cellulose::UvRect;

TEST_CASE("FaceTextures::uniform sets every face") {
	const auto textures = FaceTextures::uniform(7);
	for (int face = 0; face < 6; ++face)
		CHECK(textures[face] == 7);
}

TEST_CASE("FaceTextures::column puts top on +Y, bottom on -Y, side elsewhere") {
	const auto textures = FaceTextures::column(10, 20, 30);
	CHECK(textures[0] == 20); // +X
	CHECK(textures[1] == 20); // -X
	CHECK(textures[2] == 10); // +Y  top
	CHECK(textures[3] == 30); // -Y  bottom
	CHECK(textures[4] == 20); // +Z
	CHECK(textures[5] == 20); // -Z
}

TEST_CASE("BlockBuilder assigns textures the registry can resolve per face") {
	const auto registry =
			cellulose::BlockRegistryBuilder()
					.add_block_builder(cellulose::BlockBuilder("air"))
					.add_block_builder(cellulose::BlockBuilder("grass").texture_column(1, 2, 3))
					.add_block_builder(cellulose::BlockBuilder("stone").texture_all(4))
					.build();

	const auto grass = *registry.get_id_from_name("grass");
	CHECK(registry.face_texture(grass, 2) == 1); // top
	CHECK(registry.face_texture(grass, 0) == 2); // side
	CHECK(registry.face_texture(grass, 3) == 3); // bottom

	const auto stone = *registry.get_id_from_name("stone");
	CHECK(registry.face_texture(stone, 5) == 4);

	// air was never assigned, and out-of-range ids are inert
	CHECK(registry.face_texture(*registry.get_id_from_name("air"), 0) == 0);
	CHECK(registry.face_texture(999, 0) == 0);
}

TEST_CASE("a BlockRegistry works as a mesher texture resolver") {
	const auto registry =
			cellulose::BlockRegistryBuilder()
					.add_block_builder(cellulose::BlockBuilder("air"))
					.add_block_builder(cellulose::BlockBuilder("grass").texture_column(1, 2, 3))
					.build();

	struct FakeHot final {
		cellulose::BlockID block_id;
	};
	const FakeHot grass{ *registry.get_id_from_name("grass") };
	CHECK(registry(grass, 2) == 1);
	CHECK(registry(grass, 4) == 2);
}

TEST_CASE("TextureAtlas::layers reports a whole-sheet rect for every id") {
	const auto atlas = TextureAtlas::layers(8);
	CHECK(atlas.count() == 8);
	CHECK(atlas.rect_of(0) == UvRect{ 0.0f, 0.0f, 1.0f, 1.0f });
	CHECK(atlas.rect_of(5) == UvRect{ 0.0f, 0.0f, 1.0f, 1.0f });
}

TEST_CASE("TextureAtlas::grid places tile n at (n % cols, n / cols)") {
	const auto atlas = TextureAtlas::grid(4, 4);
	CHECK(atlas.count() == 16);

	const auto r0 = atlas.rect_of(0);
	CHECK(r0.u0 == doctest::Approx(0.0f));
	CHECK(r0.v0 == doctest::Approx(0.0f));
	CHECK(r0.u1 == doctest::Approx(0.25f));
	CHECK(r0.v1 == doctest::Approx(0.25f));

	const auto r5 = atlas.rect_of(5); // col 1, row 1
	CHECK(r5.u0 == doctest::Approx(0.25f));
	CHECK(r5.v0 == doctest::Approx(0.25f));
	CHECK(r5.u1 == doctest::Approx(0.5f));
	CHECK(r5.v1 == doctest::Approx(0.5f));
}

TEST_CASE("TextureAtlas explicit rects override and unset ids read whole-sheet") {
	TextureAtlas atlas;
	atlas.set_rect(3, UvRect{ 0.1f, 0.2f, 0.3f, 0.4f });
	CHECK(atlas.rect_of(3) == UvRect{ 0.1f, 0.2f, 0.3f, 0.4f });
	CHECK(atlas.rect_of(0) == UvRect{}); // unset
	CHECK(atlas.count() == 4);
}
