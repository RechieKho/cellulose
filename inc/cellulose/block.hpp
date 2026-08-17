#ifndef CEL_BLOCK_HPP
#define CEL_BLOCK_HPP

#include <ankerl/unordered_dense.h>
#include <optional>
#include <vector>

#include "inspect.hpp"
#include "types.hpp"

namespace cellulose {

using BlockID = u16;

namespace impl {

template <typename = void>
class BlockBuilder;

template <typename = void>
class BlockRegistryBuilder;

/// @brief data for block.
template <typename = void>
class Block final {
public:
	friend class BlockBuilder<>;

	std::string name; //!< Name of the block.
};

/// @brief Registry of blocks.
/// Instead of directly mapping the name to the block,
/// We split it into `NameIDMap` and `Store`
/// to allow access from both block id and block name
/// with priority to access via block id.
/// This is to allow renderer access the block via block id
/// from `HotCellAttribute`, while allow other to access with a more
/// user-friendly way, which is name of the block.
template <typename = void>
class BlockRegistry final {
public:
	friend class BlockRegistryBuilder<>;

	/// @brief Map from name of block to index in corresponding `BlockRegistry`.
	using NameIDMap = ankerl::unordered_dense::map<std::string, BlockID>;
	/// @brief Stores Blocks and map Block ID to block.
	using Store = std::vector<Block<>>;

private:
	NameIDMap m_name_id_map;
	Store m_store;

	BlockRegistry(NameIDMap p_name_id_map, Store p_store) : m_name_id_map(std::move(p_name_id_map)), m_store(std::move(p_store)) {}

public:
	auto inspect_block(BlockID p_block_id, Inspector<const Block<> &> p_inspector) -> void const {
		if (p_block_id >= m_store.size())
			return;
		p_inspector(m_store[p_block_id]);
	}

	auto inspect_blocks(Inspector<const Store &> p_inspector) -> void const {
		p_inspector(m_store);
	}

	auto get_id_from_name(std::string_view p_name) -> std::optional<BlockID> const {
		if (!m_name_id_map.contains(p_name))
			return std::nullopt;
		return m_name_id_map[p_name];
	}
};

/// @brief Builder to build blocks.
template <typename>
class BlockBuilder final {
public:
	std::string name;

private:
public:
	explicit BlockBuilder(std::string p_name) : name(std::move(p_name)) {}

	auto build() -> Block<> {
		if (name.length() == 0)
			throw std::logic_error(
					"`BlockBuilder`'s name must not be empty when building.");

		return Block<>{
			.name = std::move(name)
		};
	}
};

/// @brief Builder to build registry following the block builders.
template <typename>
class BlockRegistryBuilder final {
public:
private:
	std::vector<BlockBuilder<>> m_block_builders = {};

public:
	auto add_block_builder(BlockBuilder<> p_block_builder) -> BlockRegistryBuilder & {
		m_block_builders.push_back(std::move(p_block_builder));
		return *this;
	}

	auto build() -> BlockRegistry<> {
		auto name_id_map = typename BlockRegistry<>::NameIDMap();
		auto store = typename BlockRegistry<>::Store(m_block_builders.size());

		for (size i = 0; auto &builder : m_block_builders) {
			const auto &name = builder.name;

			if (name_id_map.contains(name))
				throw std::logic_error(
						fmt::format("Block name of `{}` already existed, name of block must be unique.", name));

			name_id_map[name] = i;
			store.push_back(builder.build());

			++i;
		}

		m_block_builders.clear();

		return BlockRegistry(
				std::move(name_id_map),
				std::move(store));
	}
};

} //namespace impl

} //namespace cellulose

#endif // CEL_BLOCK_HPP