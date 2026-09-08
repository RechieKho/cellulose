#ifndef CEL_CHUNK_HPP
#define CEL_CHUNK_HPP

#include "cell.hpp"
#include "coordinate.hpp"
#include "morton.hpp"
#include "types.hpp"
#include <array>

namespace cellulose {

/// @brief Number of cells along one axis of a chunk.
inline constexpr size chunk_edge_length = 32;

/// @brief Total number of cells in a chunk.
inline constexpr size chunk_cell_count = chunk_edge_length * chunk_edge_length * chunk_edge_length;

namespace impl {

/// @brief Storage for one cube of `chunk_edge_length^3` cells.
///
/// Hot attributes are stored inline as a Morton-ordered array of structures for maximum
/// density per cache line. Cold ("packed") and freezing-cold ("sparse") attributes are
/// held in the caller-selected collection types; both default to empty collections until
/// a later subsystem introduces concrete attribute types.
template <
		typename PackedCollection = PackedCellAttributeCollection<chunk_cell_count>,
		typename SparseCollection = SparseCellAttributeCollection<>>
class Chunk final {
public:
	using HotStorage = std::array<cellulose::HotCellAttribute, chunk_cell_count>;

private:
	HotStorage m_hot{};
	PackedCollection m_packed{};
	SparseCollection m_sparse{};

public:
	auto hot_attribute(CellIndex p_index) -> cellulose::HotCellAttribute & {
		return m_hot[p_index];
	}

	auto hot_attribute(CellIndex p_index) const -> const cellulose::HotCellAttribute & {
		return m_hot[p_index];
	}

	auto hot_attribute(const LocalPosition &p_position) -> cellulose::HotCellAttribute & {
		return m_hot[encode_cell_index(p_position)];
	}

	auto hot_attribute(const LocalPosition &p_position) const -> const cellulose::HotCellAttribute & {
		return m_hot[encode_cell_index(p_position)];
	}

	auto fill_hot(const cellulose::HotCellAttribute &p_value) -> void {
		m_hot.fill(p_value);
	}

	auto packed() -> PackedCollection & { return m_packed; }
	auto packed() const -> const PackedCollection & { return m_packed; }

	auto sparse() -> SparseCollection & { return m_sparse; }
	auto sparse() const -> const SparseCollection & { return m_sparse; }
};

} //namespace impl

template <
		typename PackedCollection = PackedCellAttributeCollection<chunk_cell_count>,
		typename SparseCollection = SparseCellAttributeCollection<>>
using Chunk = impl::Chunk<PackedCollection, SparseCollection>;

} //namespace cellulose

#endif // CEL_CHUNK_HPP
