# Cellulose Core Data Structure — Spec

> **Provenance.** No standalone spec doc existed when the foundation was built; the
> foundation plan treated `README.md` § "Core Data Structure" as the spec and
> recorded every concrete parameter it had to invent in an *Assumptions* table.
> This file consolidates both so the design intent survives independently of the
> plan. The synthesized, whole-library architecture doc is
> `../../ARCHITECTURE_SPEC.md`.

## 1. Design statement (from `README.md`)

World spatial data is organized in a **two-tiered hierarchy**: world chunks are
indexed in a Robin-Hood hash table; voxel data within each chunk is stored in
Morton-coded 1D arrays.

- **Robin-Hood hash table** (world → chunk): chosen for fast insert/lookup and
  high cache locality from a contiguous layout. Reference implementation:
  [`unordered_dense`](https://github.com/martinus/unordered_dense). A single
  world-level table eliminates repeated hash-computation overhead.
- **Morton-coded 1D array** (chunk → cell): preserves 3D spatial locality inside a
  chunk. Reference implementation:
  [`libmorton`](https://github.com/Forceflow/libmorton).

### Data layout by access frequency (64-byte L1 line utilization)

| Tier | Size / example | Layout | Storage |
|------|----------------|--------|---------|
| **Hot** — accessed every frame (meshing flags) | < 4 bytes | Array of Structures (AoS), Morton-ordered 1D array | inline `std::array` in the chunk |
| **Cold** — accessed often, not every frame (fluid properties) | > 8 bytes | Structure of Arrays (SoA), Morton-ordered 1D arrays, one per field | "packed" attribute collection |
| **Freezing cold** — rarely accessed (complex tile entities) | very large | sparse list | "sparse" attribute collection |

Rationale: keep hot elements small so density per cache line is maximal; keep
cold fields out of the hot line (SoA) to avoid cache pollution; keep heavy rare
data off the main memory path entirely (sparse).

## 2. Concrete parameters (Assumptions A1–A12, locked by the foundation plan)

| # | Parameter | Value | Rationale |
|---|-----------|-------|-----------|
| A1 | Chunk edge length | `32` (⇒ `32³ = 32768` cells) | Common MC-style section size; Morton code fits in 15 bits. |
| A2 | Cell index type | `CellIndex = u32` via `libmorton::morton3D_32_encode` | 15 bits needed; `u32` is libmorton's smallest 3D output. |
| A3 | Local coordinate | `LocalPosition { u8 x, y, z; }`, each `[0, 32)` | 1 byte/axis is ample. |
| A4 | Chunk-grid coordinate | `ChunkPosition { i32 x, y, z; }`, `sizeof == 12`, padding-free | Signed ±2³¹ chunks/axis ≫ any practical world; raw-byte hashable. |
| A5 | Absolute block coordinate | `WorldPosition { i64 x, y, z; }` | 64-bit signed avoids overflow in `chunk·32 + local`. |
| A6 | World↔chunk split | `chunk = world >> 5`, `local = world & 31` | Exact floor-div/mod for a power-of-two edge; C++20 guarantees arithmetic right shift for signed ints, so negatives floor correctly. |
| A7 | Hot storage | `std::array<HotCellAttribute, 32768>`, AoS, Morton-ordered | Matches README "Hot Data … AoS … Morton-coded 1D array". `HotCellAttribute` is 4 bytes. |
| A8 | Cold / freezing storage | `Chunk` templated on a packed-collection type and a sparse-collection type, **both default to the empty collection** | No concrete cold/freezing type exists yet; later subsystems (lighting, tile entities) supply them. `Chunk<>` stays zero-overhead. |
| A9 | Chunk creation | `world.chunk(pos)` = get-or-create (default-constructs); `world.find_chunk(pos)` = lookup-only → `ChunkType*` / `nullptr` | Mirrors `map::operator[]` vs `find`; explicit names avoid the footgun. |
| A10 | `ChunkPosition` hashing | standalone `ChunkPositionHash` functor, `using is_avalanching = void;`, delegates to `ankerl::unordered_dense::detail::wyhash::hash(&p, sizeof p)` | `unordered_dense` requires an avalanching hash; hashing the 12 contiguous padding-free bytes is simplest and deterministic (hardcoded seed). |
| A11 | Test framework | `doctest` v2.4.11 via `FetchContent`; single `cellulose_tests` binary; `doctest_discover_tests` registers each case with CTest | Single-header, fastest compile, minimal wiring for a header-only lib. |
| A12 | Test build toggle | `option(CELLULOSE_BUILD_TESTS …)` defaulting ON only when `cellulose` is the top-level project | Consumers embedding via `add_subdirectory` should not pay for its tests. |

## 3. Public surface produced by the foundation

All header-only under `inc/cellulose/`. `impl::` templates surfaced via `using`
aliases in `namespace cellulose` (house style).

- `coordinate.hpp` — `LocalPosition`, `ChunkPosition`, `WorldPosition` (all
  `final`, defaulted `operator==`); `ChunkPositionHash`; free functions
  `to_chunk_position`, `to_local_position`, `to_world_position`.
- `morton.hpp` — `CellIndex`; `encode_cell_index(LocalPosition) -> CellIndex`;
  `decode_cell_index(CellIndex) -> LocalPosition`.
- `chunk.hpp` — `chunk_edge_length = 32`, `chunk_cell_count = 32768`;
  alias template `Chunk<PackedCollection = PackedCellAttributeCollection<chunk_cell_count>,
  SparseCollection = SparseCellAttributeCollection<>>` with
  `hot_attribute(LocalPosition|CellIndex)` (+ const), `fill_hot`, `packed()`,
  `sparse()` (+ const).
- `world.hpp` — alias template `World<ChunkType = Chunk<>>` over
  `ankerl::unordered_dense::map<ChunkPosition, ChunkType, ChunkPositionHash>`:
  `has_chunk`, `find_chunk` (+ const), `chunk` (get-or-create), `remove_chunk`,
  `chunk_count`, `for_each_chunk(Visitor)`, `find_hot_attribute(WorldPosition)`
  (+ const, `nullptr` when the chunk is absent).
- `cellulose.hpp` — umbrella header.

Pre-existing primitives integrated: `HotCellAttribute` and the two
`*CellAttributeCollection` templates (`cell.hpp`), `Block` / `BlockRegistry` /
builders (`block.hpp`), `types.hpp`, `inspect.hpp`.

## 4. Locked rulings carried from execution (see the ledger for full text)

- **R1** — every implementer commit ends with the two attribution trailers.
- **R2** — `coordinate.hpp` must **not** depend on `chunk.hpp`; it uses the
  literals `5` (shift) / `31` (mask) with an explanatory comment rather than a
  `static_assert` tying it to `chunk_edge_length`. Revisit if the edge length
  ever changes.
- **R3** — the Task-1 vacuous smoke test was transient harness bring-up (deleted
  in Task 2); not test debt.
- **R5** — `ChunkPositionHash` reaching into `ankerl::unordered_dense::detail::`
  is sanctioned (A10); pinned by the submodule, low risk across versions.

## 5. Explicit non-goals of the foundation

Concurrency / thread-safety, spatial querying, and the rendering/meshing pipeline
are **out of scope** — each gets its own plan built on top of these `Chunk` /
`World` types. See `../../REMAINING_TASKS.md`.
