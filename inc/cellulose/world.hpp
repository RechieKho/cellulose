#ifndef CEL_WORLD_HPP
#define CEL_WORLD_HPP

#include "cell.hpp"
#include "chunk.hpp"
#include "coordinate.hpp"
#include "sync.hpp"
#include "types.hpp"
#include <ankerl/unordered_dense.h>
#include <array>
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

/// @brief The set of loaded chunks, indexed by chunk coordinate in densely-stored
/// hash tables (`ankerl::unordered_dense`).
///
/// The **directory** is **sharded**: `shard_count` independent
/// `{ std::shared_mutex, sub-map }` pairs, a chunk position routed to one by the
/// low bits of its hash. Each shard lock is shared for lookups, exclusive for
/// load (`chunk`) / unload (`remove_chunk`), so lookups and edits to unrelated
/// chunks — including a streaming thread loading elsewhere — do not contend on a
/// single reader-count cache line (benchmark D5). `for_each_chunk` and
/// `chunk_count` lock every shard (shared, in index order). Per-chunk voxel data
/// is guarded separately by the locks inside each `Chunk`.
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

	/// Directory shard count — a power of two so routing is a mask. 16 covers the
	/// thread counts the benchmarks exercised with headroom.
	static constexpr size shard_count = 16;

	using StoredChunk = std::conditional_t<is_shared, std::shared_ptr<ChunkType>, std::unique_ptr<ChunkType>>;
	using ChunkMap = ankerl::unordered_dense::map<ChunkPosition, StoredChunk, ChunkPositionHash>;

	struct Shard final {
		mutable std::shared_mutex mutex;
		ChunkMap chunks;
	};

	std::array<Padded<Shard>, shard_count> m_shards;

	auto shard_for(const ChunkPosition &p_position) -> Shard & {
		return m_shards[ChunkPositionHash{}(p_position) & (shard_count - 1)].value;
	}
	auto shard_for(const ChunkPosition &p_position) const -> const Shard & {
		return m_shards[ChunkPositionHash{}(p_position) & (shard_count - 1)].value;
	}

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
		const Shard &shard = shard_for(p_position);
		const std::shared_lock guard(shard.mutex);
		return shard.chunks.contains(p_position);
	}

	auto find_chunk(const ChunkPosition &p_position) -> ChunkHandle {
		Shard &shard = shard_for(p_position);
		const std::shared_lock guard(shard.mutex);
		const auto iterator = shard.chunks.find(p_position);
		if (iterator == shard.chunks.end())
			return ChunkHandle{ nullptr };
		if constexpr (is_shared)
			return iterator->second;
		else
			return iterator->second.get();
	}

	auto find_chunk(const ChunkPosition &p_position) const -> ConstChunkHandle {
		const Shard &shard = shard_for(p_position);
		const std::shared_lock guard(shard.mutex);
		const auto iterator = shard.chunks.find(p_position);
		if (iterator == shard.chunks.end())
			return ConstChunkHandle{ nullptr };
		if constexpr (is_shared)
			return iterator->second;
		else
			return iterator->second.get();
	}

	auto chunk(const ChunkPosition &p_position) -> ChunkType & {
		Shard &shard = shard_for(p_position);
		const std::unique_lock guard(shard.mutex);
		const auto [iterator, inserted] = shard.chunks.try_emplace(p_position);
		if (inserted)
			iterator->second = make_stored_chunk();
		return *iterator->second;
	}

	auto remove_chunk(const ChunkPosition &p_position) -> bool {
		Shard &shard = shard_for(p_position);
		const std::unique_lock guard(shard.mutex);
		return shard.chunks.erase(p_position) != 0;
	}

	auto chunk_count() const -> size {
		size total = 0;
		for (const auto &shard : m_shards) {
			const std::shared_lock guard(shard.value.mutex);
			total += shard.value.chunks.size();
		}
		return total;
	}

	template <typename Visitor>
	auto for_each_chunk(Visitor &&p_visitor) -> void {
		std::array<std::shared_lock<std::shared_mutex>, shard_count> guards;
		for (size i = 0; i < shard_count; ++i)
			guards[i] = std::shared_lock(m_shards[i].value.mutex);
		for (auto &shard : m_shards)
			for (auto &[position, stored_chunk] : shard.value.chunks)
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
