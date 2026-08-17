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

/// @brief Read-only data for block.
template <typename = void>
class ReadOnlyBlock final {
public:
	const std::string name; //!< Name of the block.
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
	/// @brief Map from name of block to index in corresponding `BlockRegistry`.
	using NameIDMap = ankerl::unordered_dense::map<std::string, BlockID>;
	/// @brief Stores Blocks and map Block ID to block.
	using Store = std::vector<ReadOnlyBlock<>>;

private:
	NameIDMap m_name_id_map;
	Store m_store;

public:
	auto inspect_block(BlockID p_block_id, Inspector<const ReadOnlyBlock<> &> p_inspector) -> void const {
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

} //namespace impl

} //namespace cellulose

#endif // CEL_BLOCK_HPP