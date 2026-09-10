#ifndef CEL_WORLD_HPP
#define CEL_WORLD_HPP

#include "cell.hpp"
#include "chunk.hpp"
#include "coordinate.hpp"
#include "types.hpp"
#include <ankerl/unordered_dense.h>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <type_traits>

namespace cellulose {

/// @brief How a `World` owns its chunks.
enum class ChunkStorage {
	/// `std::unique_ptr` — `find_chunk` hands back a raw `ChunkType *`, valid
	/// across other chunks' inserts/erases. Unloading a chunk another thread is
	/// still using is undefined; the caller must ensure quiescence. Zero overhead.
	Unique,
	/// `std::shared_ptr` — `find_chunk` hands back a `shared_ptr` that keeps the
	/// chunk alive even after `remove_chunk` drops it from the directory. Pays an
	/// atomic refcount per lookup; use when chunks stream in/out concurrently with
	/// worker access.
	Shared,
};

namespace impl {

/// @brief The set of loaded chunks, indexed by chunk coordinate in a densely-stored hash table (`ankerl::unordered_dense`).
///
/// A `std::shared_mutex` guards the **directory** (the table): shared for lookups
/// / iteration, exclusive for load (`chunk`) and unload (`remove_chunk`).
/// Per-chunk voxel data is guarded separately by the locks inside each `Chunk`.
///
/// Chunk lifetime follows `Storage` (see `ChunkStorage`). Under `Unique` a
/// `ChunkType *` from `find_chunk` stays valid across *other* chunks' inserts and
/// erases, but `remove_chunk` requires the caller to guarantee no other thread is
/// using that chunk. Under `Shared`, `find_chunk` returns a `shared_ptr` that
/// keeps the chunk alive regardless.
template <typename ChunkType = Chunk<>, ChunkStorage Storage = ChunkStorage::Unique>
class World final {
private:
	static constexpr bool is_shared = (Storage == ChunkStorage::Shared);

	using StoredChunk = std::conditional_t<is_shared, std::shared_ptr<ChunkType>, std::unique_ptr<ChunkType>>;
	using ChunkMap = ankerl::unordered_dense::map<ChunkPosition, StoredChunk, ChunkPositionHash>;

	ChunkMap m_chunks;
	mutable std::shared_mutex m_directory_mutex;

	auto make_stored_chunk() -> StoredChunk {
		if constexpr (is_shared)
			return std::make_shared<ChunkType>();
		else
			return std::make_unique<ChunkType>();
	}

public:
	/// A handle into a chunk: a raw pointer under `Unique`, a `shared_ptr` (which
	/// pins the chunk) under `Shared`. `== nullptr` when the chunk is not loaded.
	using ChunkHandle = std::conditional_t<is_shared, std::shared_ptr<ChunkType>, ChunkType *>;
	using ConstChunkHandle = std::conditional_t<is_shared, std::shared_ptr<const ChunkType>, const ChunkType *>;
	using HotAttributeType = typename ChunkType::HotAttributeType;

	auto has_chunk(const ChunkPosition &p_position) const -> bool {
		const std::shared_lock guard(m_directory_mutex);
		return m_chunks.contains(p_position);
	}

	auto find_chunk(const ChunkPosition &p_position) -> ChunkHandle {
		const std::shared_lock guard(m_directory_mutex);
		const auto iterator = m_chunks.find(p_position);
		if (iterator == m_chunks.end())
			return ChunkHandle{ nullptr };
		if constexpr (is_shared)
			return iterator->second;
		else
			return iterator->second.get();
	}

	auto find_chunk(const ChunkPosition &p_position) const -> ConstChunkHandle {
		const std::shared_lock guard(m_directory_mutex);
		const auto iterator = m_chunks.find(p_position);
		if (iterator == m_chunks.end())
			return ConstChunkHandle{ nullptr };
		if constexpr (is_shared)
			return iterator->second;
		else
			return iterator->second.get();
	}

	auto chunk(const ChunkPosition &p_position) -> ChunkType & {
		const std::unique_lock guard(m_directory_mutex);
		const auto [iterator, inserted] = m_chunks.try_emplace(p_position);
		if (inserted)
			iterator->second = make_stored_chunk();
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

	/// @brief Pointer to the hot attribute at `p_world`, or `nullptr`.
	/// @warning Unsynchronised and unpinned — single-threaded use, or the caller
	/// holds the chunk's lock / (under `Shared`) a `find_chunk` handle.
	auto find_hot_attribute(const WorldPosition &p_world) -> HotAttributeType * {
		auto handle = find_chunk(to_chunk_position(p_world));
		if (handle == nullptr)
			return nullptr;
		return &handle->hot_attribute(to_local_position(p_world));
	}

	auto find_hot_attribute(const WorldPosition &p_world) const -> const HotAttributeType * {
		auto handle = find_chunk(to_chunk_position(p_world));
		if (handle == nullptr)
			return nullptr;
		return &handle->hot_attribute(to_local_position(p_world));
	}
};

} //namespace impl

template <typename ChunkType = Chunk<>, ChunkStorage Storage = ChunkStorage::Unique>
using World = impl::World<ChunkType, Storage>;

} //namespace cellulose

#endif // CEL_WORLD_HPP
