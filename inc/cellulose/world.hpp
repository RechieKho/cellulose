#ifndef CEL_WORLD_HPP
#define CEL_WORLD_HPP

#include "cell.hpp"
#include "chunk.hpp"
#include "coordinate.hpp"
#include "types.hpp"
#include <ankerl/unordered_dense.h>

namespace cellulose {

namespace impl {

/// @brief The set of loaded chunks, indexed by chunk coordinate in a densely-stored hash table (`ankerl::unordered_dense`).
template <typename ChunkType = Chunk<>>
class World final {
private:
	using ChunkMap = ankerl::unordered_dense::map<ChunkPosition, ChunkType, ChunkPositionHash>;

	ChunkMap m_chunks;

public:
	auto has_chunk(const ChunkPosition &p_position) const -> bool {
		return m_chunks.contains(p_position);
	}

	auto find_chunk(const ChunkPosition &p_position) -> ChunkType * {
		const auto iterator = m_chunks.find(p_position);
		return iterator == m_chunks.end() ? nullptr : &iterator->second;
	}

	auto find_chunk(const ChunkPosition &p_position) const -> const ChunkType * {
		const auto iterator = m_chunks.find(p_position);
		return iterator == m_chunks.end() ? nullptr : &iterator->second;
	}

	auto chunk(const ChunkPosition &p_position) -> ChunkType & {
		return m_chunks.try_emplace(p_position).first->second;
	}

	auto remove_chunk(const ChunkPosition &p_position) -> bool {
		return m_chunks.erase(p_position) != 0;
	}

	auto chunk_count() const -> size {
		return m_chunks.size();
	}

	template <typename Visitor>
	auto for_each_chunk(Visitor &&p_visitor) -> void {
		for (auto &[position, stored_chunk] : m_chunks)
			p_visitor(position, stored_chunk);
	}

	auto find_hot_attribute(const WorldPosition &p_world) -> cellulose::HotCellAttribute * {
		auto *stored_chunk = find_chunk(to_chunk_position(p_world));
		if (stored_chunk == nullptr)
			return nullptr;
		return &stored_chunk->hot_attribute(to_local_position(p_world));
	}

	auto find_hot_attribute(const WorldPosition &p_world) const -> const cellulose::HotCellAttribute * {
		const auto *stored_chunk = find_chunk(to_chunk_position(p_world));
		if (stored_chunk == nullptr)
			return nullptr;
		return &stored_chunk->hot_attribute(to_local_position(p_world));
	}
};

} //namespace impl

template <typename ChunkType = Chunk<>>
using World = impl::World<ChunkType>;

} //namespace cellulose

#endif // CEL_WORLD_HPP
