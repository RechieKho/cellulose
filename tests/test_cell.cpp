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

TEST_CASE("HotCellAttribute face brightness round-trips per face") {
	cellulose::HotCellAttribute attribute{0, 0};

	attribute.set_right_face_brightness(3);
	attribute.set_left_face_brightness(2);
	attribute.set_top_face_brightness(1);
	attribute.set_bottom_face_brightness(3);
	attribute.set_back_face_brightness(2);
	attribute.set_front_face_brightness(1);

	CHECK(attribute.get_right_face_brightness() == 3);
	CHECK(attribute.get_left_face_brightness() == 2);
	CHECK(attribute.get_top_face_brightness() == 1);
	CHECK(attribute.get_bottom_face_brightness() == 3);
	CHECK(attribute.get_back_face_brightness() == 2);
	CHECK(attribute.get_front_face_brightness() == 1);
}

TEST_CASE("HotCellAttribute pitch and yaw round-trip") {
	cellulose::HotCellAttribute attribute{0, 0};

	attribute.set_pitch(cellulose::HotCellAttribute::PITCH_DOWN);
	attribute.set_yaw(cellulose::HotCellAttribute::YAW_LEFT);

	CHECK(attribute.get_pitch() == cellulose::HotCellAttribute::PITCH_DOWN);
	CHECK(attribute.get_yaw() == cellulose::HotCellAttribute::YAW_LEFT);
}
