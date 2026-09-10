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

// The hot tier is read through `std::atomic_ref` by default (`read_hot` /
// `write_hot`) — no data race, ThreadSanitizer-clean. Define
// `CELLULOSE_LOOSE_ATOMICS` (CMake: `-DCELLULOSE_LOOSE_ATOMICS=ON`) to opt back
// into the plain-array benign race: writers are ~30% faster but a `write_hot`
// closure may then assign fields, and the layer is UB by the standard / flagged
// by TSan. See `docs/plans/design-followups.md` decision 3.
#if !defined(CELLULOSE_STRICT_ATOMICS) && !defined(CELLULOSE_LOOSE_ATOMICS)
#define CELLULOSE_STRICT_ATOMICS
#endif

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

#ifdef CELLULOSE_STRICT_ATOMICS
// Under `-DCELLULOSE_STRICT_ATOMICS`, `read_hot` / `write_hot` reach the hot array
// through these views so every element access is a lock-free `std::atomic_ref`
// load/store (no data race, TSan-clean) instead of the default benign race. A
// strict `write_hot` closure must assign **whole elements** (`hot[i] = value`),
// not fields (`hot[i].field = ...`).
template <typename T>
struct AtomicReadView final {
	const T *base;
	auto operator[](size p_index) const -> T {
		return std::atomic_ref<T>(const_cast<T &>(base[p_index])).load(std::memory_order_relaxed);
	}
};

template <typename T>
struct AtomicCellRef final {
	T *cell;
	operator T() const { return std::atomic_ref<T>(*cell).load(std::memory_order_relaxed); }
	auto operator=(const T &p_value) -> AtomicCellRef & {
		std::atomic_ref<T>(*cell).store(p_value, std::memory_order_relaxed);
		return *this;
	}
};

template <typename T>
struct AtomicWriteView final {
	T *base;
	auto operator[](size p_index) const -> AtomicCellRef<T> { return AtomicCellRef<T>{ base + p_index }; }
};
#endif

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
#ifdef CELLULOSE_STRICT_ATOMICS
		static_assert(
				std::atomic_ref<HotType>::is_always_lock_free,
				"CELLULOSE_STRICT_ATOMICS requires a lock-free-sized hot attribute type.");
		return m_hot_lock->sequence.read([&] {
			const impl::AtomicReadView<HotType> view{ m_hot.data() };
			return p_read(view);
		});
#else
		return m_hot_lock->sequence.read([&] { return p_read(m_hot); });
#endif
	}

	/// @brief Run `p_write(HotStorage &)` as the sole hot-tier writer. Under
	/// `CELLULOSE_STRICT_ATOMICS` the closure receives an atomic write view and
	/// must assign whole elements (`hot[i] = value`), not fields.
	template <typename WriteFn>
	auto write_hot(WriteFn &&p_write) -> void {
		const std::lock_guard writer_guard(m_hot_lock->writer);
#ifdef CELLULOSE_STRICT_ATOMICS
		m_hot_lock->sequence.write([&] {
			impl::AtomicWriteView<HotType> view{ m_hot.data() };
			p_write(view);
		});
#else
		m_hot_lock->sequence.write([&] { p_write(m_hot); });
#endif
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

	/// @brief Hot-tier seqlock retries so far — a benchmark instrument. Always 0
	/// unless built with `-DCELLULOSE_SEQLOCK_STATS=ON`.
	auto hot_retry_count() const -> u64 { return m_hot_lock->sequence.retries(); }

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
