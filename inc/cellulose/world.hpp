#ifndef CEL_WORLD_HPP
#define CEL_WORLD_HPP

#include "cell.hpp"
#include "chunk.hpp"
#include "coordinate.hpp"
#include "types.hpp"
#include <ankerl/unordered_dense.h>
#include <memory>
#include <shared_mutex>

namespace cellulose {

namespace impl {

/// @brief The set of loaded chunks, indexed by chunk coordinate in a densely-stored hash table (`ankerl::unordered_dense`).
///
/// Chunks are held behind `std::unique_ptr`, so a `ChunkType *` handed out by
/// `find_chunk` / `chunk` stays valid across later directory inserts **and**
/// erases — only the pointer slot in the table ever moves. A `std::shared_mutex`
/// guards the **directory** (the table itself): shared for lookups / iteration,
/// exclusive for load (`chunk`) and unload (`remove_chunk`). Per-chunk voxel data
/// is guarded separately by the locks inside each `Chunk`.
///
/// @warning `remove_chunk` currently requires the caller to guarantee no other
/// thread is accessing that chunk; safe reclamation under live readers is a
/// later increment.
template <typename ChunkType = Chunk<>>
class World final {
private:
	using ChunkMap = ankerl::unordered_dense::map<ChunkPosition, std::unique_ptr<ChunkType>, ChunkPositionHash>;

	ChunkMap m_chunks;
	mutable std::shared_mutex m_directory_mutex;

public:
	auto has_chunk(const ChunkPosition &p_position) const -> bool {
		const std::shared_lock guard(m_directory_mutex);
		return m_chunks.contains(p_position);
	}

	auto find_chunk(const ChunkPosition &p_position) -> ChunkType * {
		const std::shared_lock guard(m_directory_mutex);
		const auto iterator = m_chunks.find(p_position);
		return iterator == m_chunks.end() ? nullptr : iterator->second.get();
	}

	auto find_chunk(const ChunkPosition &p_position) const -> const ChunkType * {
		const std::shared_lock guard(m_directory_mutex);
		const auto iterator = m_chunks.find(p_position);
		return iterator == m_chunks.end() ? nullptr : iterator->second.get();
	}

	auto chunk(const ChunkPosition &p_position) -> ChunkType & {
		const std::unique_lock guard(m_directory_mutex);
		const auto [iterator, inserted] = m_chunks.try_emplace(p_position);
		if (inserted)
			iterator->second = std::make_unique<ChunkType>();
		return *iterator->second;
	}

	auto remove_chunk(const ChunkPosition &p_position) -> bool {
		const std::unique_lock guard(m_directory_mutex);
		return m_chunks.erase(p_position) != 0;
	}

	auto chunk_count() const -> size {
		const std::shared_lock guard(m_directory_mutex);
		return m_chunks.size();
	}

	template <typename Visitor>
	auto for_each_chunk(Visitor &&p_visitor) -> void {
		const std::shared_lock guard(m_directory_mutex);
		for (auto &[position, stored_chunk] : m_chunks)
			p_visitor(position, *stored_chunk);
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
