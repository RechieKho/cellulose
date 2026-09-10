#include <doctest/doctest.h>

#include <cellulose/raycast.hpp>

#include <optional>
#include <random>

namespace {

auto solid_predicate() {
	return [](const cellulose::HotCellAttribute &p_attribute) { return p_attribute.block_id != 0; };
}

auto set_solid(cellulose::World<> &p_world, const cellulose::WorldPosition &p_cell) -> void {
	p_world.chunk(cellulose::to_chunk_position(p_cell))
			.hot_attribute(cellulose::to_local_position(p_cell))
			.block_id = 1;
}

auto is_solid_cell(cellulose::World<> &p_world, const cellulose::WorldPosition &p_cell) -> bool {
	auto *chunk = p_world.find_chunk(cellulose::to_chunk_position(p_cell));
	return chunk != nullptr &&
			chunk->hot_attribute(cellulose::to_local_position(p_cell)).block_id != 0;
}

} //namespace

TEST_CASE("axis-aligned ray hits the first solid with the entry face and distance") {
	cellulose::World<> world;
	set_solid(world, { 5, 0, 0 });

	const auto hit = cellulose::raycast(
			world, cellulose::Ray{ { 0.5, 0.5, 0.5 }, { 1, 0, 0 } }, 100.0, solid_predicate());

	REQUIRE(hit.has_value());
	CHECK(hit->cell == cellulose::WorldPosition{ 5, 0, 0 });
	CHECK(hit->normal == cellulose::Vec3i{ -1, 0, 0 });
	CHECK(hit->distance == doctest::Approx(4.5));
}

TEST_CASE("a ray starting inside a solid reports that cell at distance zero") {
	cellulose::World<> world;
	set_solid(world, { 0, 0, 0 });

	const auto hit = cellulose::raycast(
			world, cellulose::Ray{ { 0.5, 0.5, 0.5 }, { 1, 0, 0 } }, 100.0, solid_predicate());

	REQUIRE(hit.has_value());
	CHECK(hit->cell == cellulose::WorldPosition{ 0, 0, 0 });
	CHECK(hit->normal == cellulose::Vec3i{ 0, 0, 0 });
	CHECK(hit->distance == doctest::Approx(0.0));
}

TEST_CASE("a ray into empty space or past its reach misses") {
	cellulose::World<> world;
	CHECK_FALSE(cellulose::raycast(
			world, cellulose::Ray{ { 0.5, 0.5, 0.5 }, { 1, 0, 0 } }, 100.0, solid_predicate())
					.has_value());

	set_solid(world, { 5, 0, 0 });
	CHECK_FALSE(cellulose::raycast(
			world, cellulose::Ray{ { 0.5, 0.5, 0.5 }, { 1, 0, 0 } }, 3.0, solid_predicate())
					.has_value());
}

TEST_CASE("a ray crosses a chunk boundary and hits a solid in the next chunk") {
	cellulose::World<> world;
	set_solid(world, { 32, 0, 0 }); // chunk (1,0,0), local (0,0,0)

	const auto hit = cellulose::raycast(
			world, cellulose::Ray{ { 0.5, 0.5, 0.5 }, { 1, 0, 0 } }, 100.0, solid_predicate());

	REQUIRE(hit.has_value());
	CHECK(hit->cell == cellulose::WorldPosition{ 32, 0, 0 });
	CHECK(hit->normal == cellulose::Vec3i{ -1, 0, 0 });
	CHECK(hit->distance == doctest::Approx(31.5));
}

TEST_CASE("a negative-direction ray hits the +face of the blocker") {
	cellulose::World<> world;
	set_solid(world, { -5, 0, 0 });

	const auto hit = cellulose::raycast(
			world, cellulose::Ray{ { 0.5, 0.5, 0.5 }, { -1, 0, 0 } }, 100.0, solid_predicate());

	REQUIRE(hit.has_value());
	CHECK(hit->cell == cellulose::WorldPosition{ -5, 0, 0 });
	CHECK(hit->normal == cellulose::Vec3i{ 1, 0, 0 });
	CHECK(hit->distance == doctest::Approx(4.5));
}

TEST_CASE("DDA cell selection matches a fine-step reference for random rays") {
	cellulose::World<> world;
	std::mt19937 rng(1234);
	std::bernoulli_distribution solid(0.12);

	for (int x = 0; x < 16; ++x)
		for (int y = 0; y < 16; ++y)
			for (int z = 0; z < 16; ++z)
				if (solid(rng))
					set_solid(world, { x, y, z });

	std::uniform_real_distribution<double> start(0.0, 16.0);
	std::uniform_real_distribution<double> component(-1.0, 1.0);

	for (int i = 0; i < 200; ++i) {
		const cellulose::Ray ray{
			{ start(rng), start(rng), start(rng) },
			{ component(rng), component(rng), component(rng) }
		};
		if (cellulose::normalized(ray.direction) == cellulose::Vec3d{ 0, 0, 0 })
			continue;

		constexpr double max_distance = 40.0;
		const auto direction = cellulose::normalized(ray.direction);

		std::optional<cellulose::WorldPosition> reference;
		for (double t = 0.0; t <= max_distance; t += 0.002) {
			const auto cell = cellulose::to_cell(ray.origin + direction * t);
			if (is_solid_cell(world, cell)) {
				reference = cell;
				break;
			}
		}

		const auto hit = cellulose::raycast(world, ray, max_distance, solid_predicate());
		if (reference.has_value()) {
			REQUIRE(hit.has_value());
			CHECK(hit->cell == *reference);
		} else {
			CHECK_FALSE(hit.has_value());
		}
	}
}
