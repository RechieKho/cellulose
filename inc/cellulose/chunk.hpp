#ifndef CEL_CHUNK_HPP
#define CEL_CHUNK_HPP

#include "cell.hpp"
#include "coordinate.hpp"
#include "morton.hpp"
#include "rwlock.hpp"
#include "seqlock.hpp"
#include "sync.hpp"
#include "types.hpp"
#include <array>
#include <atomic>
#include <mutex>

namespace cellulose {

/// @brief Number of cells along one axis of a chunk.
inline constexpr size chunk_edge_length = 32;

/// @brief Total number of cells in a chunk.
inline constexpr size chunk_cell_count = chunk_edge_length * chunk_edge_length * chunk_edge_length;

// Ties `coordinate.hpp`'s hardcoded shift/mask to the edge length (that header
// must stay independent of this one — see `chunk_edge_length_shift`).
static_assert(
		chunk_edge_length == (size{ 1 } << chunk_edge_length_shift),
		"`chunk_edge_length` must equal `1 << chunk_edge_length_shift`.");

/// @brief `PackedCellAttributeCollection` sized for one chunk — the ergonomic
/// spelling for a cold-tier attribute pack (`Chunk<Hot, PackedChunkAttributes<Fluid>>`).
template <typename... Attributes>
using PackedChunkAttributes = PackedCellAttributeCollection<chunk_cell_count, Attributes...>;

/// @brief `SparseCellAttributeCollection` — the freezing-cold-tier attribute pack.
template <typename... Attributes>
using SparseChunkAttributes = SparseCellAttributeCollection<Attributes...>;

namespace impl {

/// @brief A seqlock plus the mutex that serialises its writers, for one attribute tier.
///
/// `cellulose::` qualification is required: inside `namespace cellulose::impl` the
/// bare names `SeqLock` / `RWLock` / `Padded` bind to the unspecialised templates.
struct TierLock final {
	cellulose::SeqLock sequence;
	std::mutex writer;
};

/// @brief Storage for one cube of `chunk_edge_length^3` cells.
///
/// Hot attributes are stored inline as a Morton-ordered array of structures for maximum
/// density per cache line. The hot type is a template parameter (`HotCellAttribute` by
/// default); a consumer may substitute any `HotAttribute` type. Cold ("packed") and
/// freezing-cold ("sparse") attributes are held in the caller-selected collection types;
/// both default to empty collections — the base library ships no concrete cold/freezing
/// attributes.
///
/// Thread safety is per tier. Prefer the functor accessors — `read_hot` / `write_hot`,
/// `read_cold` / `write_cold`, `read_sparse` / `write_sparse` — which run the caller's
/// closure under the right lock. The hot and cold tiers use a `SeqLock` (optimistic,
/// retrying reads over the fixed-size arrays); the sparse tier uses an `RWLock` because
/// its map may reallocate. The bare accessors below are **not** synchronised.
///
/// `revision()` is a monotonically increasing counter bumped by every `write_*` — a
/// cheap "this chunk may have changed since you last looked" signal for meshing,
/// networking or persistence. It is conservative: it advances even on a no-op write.
///
/// The chunk and every embedded lock are aligned to `cache_line_size` so no two locks —
/// and no lock and the voxel arrays — share a cache line.
template <
		HotAttribute HotType = cellulose::HotCellAttribute,
		typename PackedCollection = PackedCellAttributeCollection<chunk_cell_count>,
		typename SparseCollection = SparseCellAttributeCollection<>>
class alignas(cache_line_size) Chunk final {
public:
	using HotAttributeType = HotType;
	using HotStorage = std::array<HotType, chunk_cell_count>;

private:
	cellulose::Padded<TierLock> m_hot_lock;
	cellulose::Padded<TierLock> m_cold_lock;
	cellulose::Padded<cellulose::RWLock> m_sparse_lock;
	cellulose::Padded<std::atomic<u64>> m_revision;

	HotStorage m_hot{};
	PackedCollection m_packed{};
	SparseCollection m_sparse{};

	auto bump_revision() -> void {
		m_revision->fetch_add(1, std::memory_order_release);
	}

public:
	// --- Synchronised functor accessors -------------------------------------

	/// @brief Run `p_read(const HotStorage &)` under the hot seqlock, returning its
	/// (by-value) result. The closure may observe a torn snapshot mid-write; it is
	/// retried. Never index past `chunk_cell_count`.
	template <typename ReadFn>
	auto read_hot(ReadFn &&p_read) const {
		return m_hot_lock->sequence.read([&] { return p_read(m_hot); });
	}

	/// @brief Run `p_write(HotStorage &)` as the sole hot-tier writer.
	template <typename WriteFn>
	auto write_hot(WriteFn &&p_write) -> void {
		const std::lock_guard writer_guard(m_hot_lock->writer);
		m_hot_lock->sequence.write([&] { p_write(m_hot); });
		bump_revision();
	}

	/// @brief Run `p_read(const PackedCollection &)` under the cold seqlock.
	template <typename ReadFn>
	auto read_cold(ReadFn &&p_read) const {
		return m_cold_lock->sequence.read([&] { return p_read(m_packed); });
	}

	/// @brief Run `p_write(PackedCollection &)` as the sole cold-tier writer.
	template <typename WriteFn>
	auto write_cold(WriteFn &&p_write) -> void {
		const std::lock_guard writer_guard(m_cold_lock->writer);
		m_cold_lock->sequence.write([&] { p_write(m_packed); });
		bump_revision();
	}

	/// @brief Run `p_read(const SparseCollection &)` under a shared sparse lock.
	template <typename ReadFn>
	auto read_sparse(ReadFn &&p_read) const {
		return m_sparse_lock->read([&] { return p_read(m_sparse); });
	}

	/// @brief Run `p_write(SparseCollection &)` under the exclusive sparse lock.
	template <typename WriteFn>
	auto write_sparse(WriteFn &&p_write) -> void {
		m_sparse_lock->write([&] { p_write(m_sparse); });
		bump_revision();
	}

	/// @brief Monotonic change counter; advances on every `write_*` (conservatively).
	auto revision() const -> u64 {
		return m_revision->load(std::memory_order_acquire);
	}

	// --- Unsynchronised accessors -----------------------------------------
	// Single-threaded use, or the caller already holds the matching tier lock.

	auto hot_attribute(CellIndex p_index) -> HotType & {
		return m_hot[p_index];
	}

	auto hot_attribute(CellIndex p_index) const -> const HotType & {
		return m_hot[p_index];
	}

	auto hot_attribute(const LocalPosition &p_position) -> HotType & {
		return m_hot[encode_cell_index(p_position)];
	}

	auto hot_attribute(const LocalPosition &p_position) const -> const HotType & {
		return m_hot[encode_cell_index(p_position)];
	}

	auto fill_hot(const HotType &p_value) -> void {
		m_hot.fill(p_value);
	}

	auto packed() -> PackedCollection & { return m_packed; }
	auto packed() const -> const PackedCollection & { return m_packed; }

	auto sparse() -> SparseCollection & { return m_sparse; }
	auto sparse() const -> const SparseCollection & { return m_sparse; }
};

} //namespace impl

template <
		HotAttribute HotType = HotCellAttribute,
		typename PackedCollection = PackedCellAttributeCollection<chunk_cell_count>,
		typename SparseCollection = SparseCellAttributeCollection<>>
using Chunk = impl::Chunk<HotType, PackedCollection, SparseCollection>;

static_assert(
		alignof(Chunk<>) >= cache_line_size,
		"`Chunk` must be cache-line aligned so its locks cannot false-share.");

} //namespace cellulose

#endif // CEL_CHUNK_HPP
