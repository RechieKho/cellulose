#include <doctest/doctest.h>

#include <cellulose/block.hpp>

#include <string>

TEST_CASE("BlockRegistryBuilder assigns contiguous ids and no phantom blocks") {
	const auto registry = cellulose::BlockRegistryBuilder()
								  .add_block_builder(cellulose::BlockBuilder("stone"))
								  .add_block_builder(cellulose::BlockBuilder("dirt"))
								  .add_block_builder(cellulose::BlockBuilder("grass"))
								  .build();

	const auto stone = registry.get_id_from_name("stone");
	const auto dirt = registry.get_id_from_name("dirt");
	const auto grass = registry.get_id_from_name("grass");

	REQUIRE(stone.has_value());
	REQUIRE(dirt.has_value());
	REQUIRE(grass.has_value());
	CHECK(*stone == 0);
	CHECK(*dirt == 1);
	CHECK(*grass == 2);
	CHECK_FALSE(registry.get_id_from_name("water").has_value());

	std::size_t block_count = 0;
	registry.inspect_blocks([&](const auto &p_store) { block_count = p_store.size(); });
	CHECK(block_count == 3);
}

TEST_CASE("a block id resolves to its named block, and out-of-range ids are inert") {
	const auto registry = cellulose::BlockRegistryBuilder()
								  .add_block_builder(cellulose::BlockBuilder("stone"))
								  .add_block_builder(cellulose::BlockBuilder("dirt"))
								  .build();

	std::string resolved;
	registry.inspect_block(
			*registry.get_id_from_name("dirt"),
			[&](const cellulose::Block &p_block) { resolved = p_block.name; });
	CHECK(resolved == "dirt");

	bool visited = false;
	registry.inspect_block(7, [&](const cellulose::Block &) { visited = true; });
	CHECK_FALSE(visited);
}

TEST_CASE("duplicate block names are rejected") {
	CHECK_THROWS_AS(
			cellulose::BlockRegistryBuilder()
					.add_block_builder(cellulose::BlockBuilder("stone"))
					.add_block_builder(cellulose::BlockBuilder("stone"))
					.build(),
			std::logic_error);
}
