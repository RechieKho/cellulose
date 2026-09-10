#ifndef CEL_BLOCK_HPP
#define CEL_BLOCK_HPP

#include <ankerl/unordered_dense.h>
#include <array>
#include <optional>
#include <vector>

#include "inspect.hpp"
#include "types.hpp"

namespace cellulose {

using BlockID = u16;

/// @brief A texture identifier, meaningful only to the consumer's renderer. In
/// the raylib array-texture bridge it is the `sampler2DArray` layer index. `0` is
/// the conventional "unset / default" value.
using TextureID = u32;

/// @brief The texture id for each of a block's six faces, in the library's
/// canonical face order: `0` +X (right), `1` -X (left), `2` +Y (top),
/// `3` -Y (bottom), `4` +Z (back), `5` -Z (front) — the same order as
/// `face_brightness` and every spatial query.
struct FaceTextures final {
	std::array<TextureID, 6> faces{};

	friend auto operator==(const FaceTextures &, const FaceTextures &) -> bool = default;

	/// @brief The same texture on all six faces.
	static constexpr auto uniform(TextureID p_texture) -> FaceTextures {
		return FaceTextures{ { p_texture, p_texture, p_texture, p_texture, p_texture, p_texture } };
	}

	/// @brief A Minecraft-style column: distinct top and bottom, `p_side` on the
	/// four horizontal faces (grass, logs, sandstone, …).
	static constexpr auto column(TextureID p_top, TextureID p_side, TextureID p_bottom) -> FaceTextures {
		return FaceTextures{ { p_side, p_side, p_top, p_bottom, p_side, p_side } };
	}

	constexpr auto operator[](i32 p_face) const -> TextureID {
		return faces[static_cast<size>(p_face)];
	}
};

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
	FaceTextures textures{}; //!< Per-face texture ids for the renderer (all `0` unless set).
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
	auto inspect_block(BlockID p_block_id, Inspector<const Block<> &> p_inspector) const -> void {
		if (p_block_id >= m_store.size())
			return;
		p_inspector(m_store[p_block_id]);
	}

	auto inspect_blocks(Inspector<const Store &> p_inspector) const -> void {
		p_inspector(m_store);
	}

	auto get_id_from_name(const std::string &p_name) const -> std::optional<BlockID> {
		if (!m_name_id_map.contains(p_name))
			return std::nullopt;
		return m_name_id_map.at(p_name);
	}

	/// @brief The texture id assigned to face `p_face` (`0..5`, canonical order)
	/// of block `p_block_id`. `0` when the id is out of range or the face was
	/// never assigned.
	auto face_texture(BlockID p_block_id, i32 p_face) const -> TextureID {
		if (p_block_id >= m_store.size())
			return 0;
		return m_store[p_block_id].textures[p_face];
	}

	/// @brief The registry is itself a mesher texture resolver: pass it as the
	/// `texture_of` argument to `mesh_chunk` and it maps `(hot attribute, face)`
	/// to a texture id via the attribute's `block_id`.
	template <typename HotType>
	auto operator()(const HotType &p_attribute, i32 p_face) const -> TextureID {
		return face_texture(static_cast<BlockID>(p_attribute.block_id), p_face);
	}
};

/// @brief Builder to build blocks.
template <typename>
class BlockBuilder final {
public:
	std::string name;
	FaceTextures textures{};

private:
public:
	explicit BlockBuilder(std::string p_name) : name(std::move(p_name)) {}

	/// @brief Assign every face's texture id explicitly.
	auto texture(FaceTextures p_textures) -> BlockBuilder & {
		textures = p_textures;
		return *this;
	}

	/// @brief Assign the same texture id to all six faces.
	auto texture_all(TextureID p_texture) -> BlockBuilder & {
		textures = FaceTextures::uniform(p_texture);
		return *this;
	}

	/// @brief Assign a Minecraft-style column (distinct top / bottom, `p_side`
	/// around).
	auto texture_column(TextureID p_top, TextureID p_side, TextureID p_bottom) -> BlockBuilder & {
		textures = FaceTextures::column(p_top, p_side, p_bottom);
		return *this;
	}

	auto build() -> Block<> {
		if (name.length() == 0)
			throw std::logic_error(
					"`BlockBuilder`'s name must not be empty when building.");

		return Block<>{
			.name = std::move(name),
			.textures = textures
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
		auto store = typename BlockRegistry<>::Store();
		store.reserve(m_block_builders.size());

		for (size i = 0; auto &builder : m_block_builders) {
			const auto &name = builder.name;

			if (name_id_map.contains(name))
				throw std::logic_error(
						fmt::format("Block name of `{}` already existed, name of block must be unique.", name));

			name_id_map[name] = static_cast<BlockID>(i);
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

using Block = impl::Block<>;
using BlockRegistry = impl::BlockRegistry<>;
using BlockBuilder = impl::BlockBuilder<>;
using BlockRegistryBuilder = impl::BlockRegistryBuilder<>;

} //namespace cellulose

#endif // CEL_BLOCK_HPP