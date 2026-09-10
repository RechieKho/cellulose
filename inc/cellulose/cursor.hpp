#ifndef CEL_CURSOR_HPP
#define CEL_CURSOR_HPP

#include "cell.hpp"
#include "chunk.hpp"
#include "coordinate.hpp"
#include "morton.hpp"
#include "types.hpp"
#include "world.hpp"
#include <optional>

namespace cellulose {

namespace impl {

/// @brief Walks world cells while caching the current chunk pointer, so a run of
/// cells in the same chunk costs one `find_chunk` (one directory-lock acquisition)
/// instead of one per cell — the common case for raycasts, swept boxes and the
/// mesher's apron. Assumes the chunks it visits are not unloaded mid-walk.
template <typename WorldType>
class ChunkCursor final {
	WorldType &m_world;
	ChunkPosition m_chunk_position{};
	bool m_resolved = false;
	decltype(m_world.find_chunk(ChunkPosition{})) m_chunk = nullptr;

	auto resolve(const ChunkPosition &p_chunk_position) -> void {
		if (m_resolved && p_chunk_position == m_chunk_position)
			return;
		m_chunk_position = p_chunk_position;
		m_chunk = m_world.find_chunk(p_chunk_position);
		m_resolved = true;
	}

public:
	explicit ChunkCursor(WorldType &p_world) : m_world(p_world) {}

	/// @brief Snapshot of the hot attribute at `p_cell` under its chunk's seqlock,
	/// or `nullopt` when that chunk is not loaded.
	auto hot(const WorldPosition &p_cell) -> std::optional<cellulose::HotCellAttribute> {
		resolve(to_chunk_position(p_cell));
		if (m_chunk == nullptr)
			return std::nullopt;
		const LocalPosition local = to_local_position(p_cell);
		return m_chunk->read_hot(
				[&](const auto &p_hot) { return p_hot[encode_cell_index(local)]; });
	}
};

template <typename WorldType>
ChunkCursor(WorldType &) -> ChunkCursor<WorldType>;

} //namespace impl

} //namespace cellulose

#endif // CEL_CURSOR_HPP
