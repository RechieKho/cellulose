# Cellulose — Architecture Specification

`cellulose` is a performant C++ voxel library providing spatial data structures
and operations for Minecraft-style voxel engines under concurrent settings.

This document is the self-contained architecture reference. It folds together the
design narrative from `README.md`, the locked parameters and rulings established
while building the foundation, and the as-built public surface. Forward work is
tracked in `REMAINING_TASKS.md`.

- **Language / build:** C++20, CMake ≥ 3.10. `libcellulose` is a header-only
  `INTERFACE` target; all library code lives in headers under `inc/cellulose/`.
- **Vendored deps:** `libmorton`, `ankerl::unordered_dense`, `fmt`, `raylib`
  (submodules / FetchContent). Test dep: `doctest` v2.5.3 via FetchContent.
- **Status:** subsystem 1 (Core Data Structure) is implemented and tested;
  subsystems 2–4 are designed but not built.

---

## Conventions (house style — follow exactly)

- Classes with logic go in `namespace cellulose::impl` as
  `template <typename = void>` (or already-parameterised) classes, then are
  surfaced via `using` / alias templates in `namespace cellulose`.
- Public data members have **no** `m_` prefix; private members keep `m_`.
  Parameters are `p_`-prefixed.
- Trailing return types everywhere (`auto f() -> T`).
- Tabs for indent, `ColumnLimit: 0`, `{ a, b }` braced-list spacing,
  `} //namespace x` closers — `.clang-format` enforces; run it on touched files.
- Include guards: `CEL_<NAME>_HPP`.
- Commit messages end with the two attribution trailers configured for the
  session (`Co-Authored-By:` + `Claude-Session:`).
- Fundamental type aliases live in `types.hpp` (`size`, `i8`…`i64`, `u8`…`u64`,
  `f32`, `f64`). Use them.

---

## 1. Core Data Structure — **implemented**

Two-tiered hierarchy: a world-level Robin-Hood hash table of chunks; a
Morton-coded 1D array of cells within each chunk. A single world-level hash table
eliminates repeated hash-computation overhead.

### 1.1 Coordinate system (`coordinate.hpp`)

| Type | Definition | Notes |
|------|------------|-------|
| `LocalPosition` | `{ u8 x, y, z; }`, each `[0, 32)` | cell within a chunk |
| `ChunkPosition` | `{ i32 x, y, z; }` | chunk in the chunk grid; `static_assert(sizeof == 12)` — padding-free so it can be hashed by raw bytes |
| `WorldPosition` | `{ i64 x, y, z; }` | absolute block position; 64-bit avoids overflow combining `chunk·32 + local` |

All three are `final` with a defaulted `operator==`.

Conversions (free functions in `namespace cellulose`):

- `to_chunk_position(WorldPosition) -> ChunkPosition` — `axis >> 5`
- `to_local_position(WorldPosition) -> LocalPosition` — `axis & 31`
- `to_world_position(ChunkPosition, LocalPosition) -> WorldPosition` —
  `(chunk << 5) | local`

`>> 5` / `& 31` are exact floor-division / modulo for the power-of-two edge
length. C++20 guarantees arithmetic right shift for signed integers, so negative
world coordinates split correctly (e.g. `WorldPosition{-1,-32,-33}` →
`ChunkPosition{-1,-1,-2}`, `LocalPosition{31,0,31}`).

**Locked (R2):** `coordinate.hpp` does **not** include `chunk.hpp`. The `5` / `31`
literals are used with an explanatory comment instead of referencing
`chunk_edge_length`, to keep the dependency direction clean. If the edge length
ever changes, these must change with it.

### 1.2 Hashing (`ChunkPositionHash`)

Standalone functor, `using is_avalanching = void;`, delegates to
`ankerl::unordered_dense::detail::wyhash::hash(&pos, sizeof pos)`.
`unordered_dense` requires an avalanching hash; hashing the 12 contiguous
padding-free bytes is the simplest deterministic choice (the wyhash seed is
hardcoded, so `hash(a) != hash(b)` comparisons are stable). Reaching into
`detail::` is sanctioned (the submodule pins the version). If an exotic platform
ever padded `i32[3]`, this would need a `std::tuple` fallback.

### 1.3 Morton cell index (`morton.hpp`)

- `using CellIndex = u32;`
- `encode_cell_index(LocalPosition) -> CellIndex` — thin wrapper over
  `libmorton::morton3D_32_encode`
- `decode_cell_index(CellIndex) -> LocalPosition` — over `morton3D_32_decode`

Over the 5-bit-per-axis domain this is a bijection onto `[0, 2^15)` (verified
exhaustively for all 32768 positions). `decode_cell_index` is only well-defined
for indices produced by `encode_cell_index` from an in-range `LocalPosition`.

### 1.4 Chunk (`chunk.hpp`)

- `inline constexpr size chunk_edge_length = 32;`
- `inline constexpr size chunk_cell_count = 32768;`
- `Chunk<PackedCollection = PackedCellAttributeCollection<chunk_cell_count>,
   SparseCollection = SparseCellAttributeCollection<>>` — `final`,
  default-constructible (hot array zero-initialised).

| Member (+ const overload) | Purpose |
|---------------------------|---------|
| `hot_attribute(LocalPosition) -> HotCellAttribute&` | address by 3D position (Morton-encoded internally) |
| `hot_attribute(CellIndex) -> HotCellAttribute&` | address by raw Morton index |
| `fill_hot(const HotCellAttribute&)` | set every cell |
| `packed() -> PackedCollection&` | cold-tier SoA collection |
| `sparse() -> SparseCollection&` | freezing-cold sparse collection |

Storage tiers (from `README.md`, keyed by access frequency for 64-byte L1 line
utilization):

| Tier | Example | Layout | Backing |
|------|---------|--------|---------|
| **Hot** (< 4 bytes, every frame) | meshing flags | AoS, Morton-ordered | `std::array<HotCellAttribute, 32768>` inline (~131 KB) |
| **Cold** (> 8 bytes, not every frame) | fluid properties | SoA, one Morton array per field | `packed()` collection |
| **Freezing cold** (very large, rare) | complex tile entities | sparse list | `sparse()` collection |

**Locked (A8):** no concrete cold/freezing attribute type exists yet — both
collection type parameters default to the *empty* collection so `Chunk<>` is
zero-overhead. Later subsystems (lighting, tile entities) supply real types.

**Name-lookup gotcha:** inside `namespace cellulose::impl`, the unqualified name
`HotCellAttribute` binds to the class *template* `impl::HotCellAttribute<>`, not
the `cellulose::HotCellAttribute` alias. `chunk.hpp` and `world.hpp` qualify it as
`cellulose::HotCellAttribute` at the affected sites. Any new `impl::` code that
names `HotCellAttribute` as a type must do the same.

### 1.5 World (`world.hpp`)

`World<ChunkType = Chunk<>>` over
`ankerl::unordered_dense::map<ChunkPosition, ChunkType, ChunkPositionHash>`,
`final`:

| Member | Semantics |
|--------|-----------|
| `has_chunk(ChunkPosition) const -> bool` | |
| `find_chunk(ChunkPosition) -> ChunkType*` (+ const) | lookup-only, `nullptr` when absent |
| `chunk(ChunkPosition) -> ChunkType&` | **get-or-create** (default-constructs via `try_emplace`) |
| `remove_chunk(ChunkPosition) -> bool` | `true` if a chunk was erased |
| `chunk_count() const -> size` | |
| `for_each_chunk(Visitor&&)` | invokes `visitor(const ChunkPosition&, ChunkType&)` |
| `find_hot_attribute(WorldPosition) -> HotCellAttribute*` (+ const) | resolve through the containing chunk; `nullptr` when that chunk is absent |

**Known limitation (design-accepted, C1):** `unordered_dense::map` value-stores
the ~131 KB `Chunk`. Any insert can rehash and move every chunk, invalidating held
`Chunk*` / `HotCellAttribute*`. The foundation is correct for immediate-use
access only — **callers must not retain those pointers across inserts.** The
concurrency subsystem should evaluate `unordered_dense::segmented_map` or
`unique_ptr<Chunk>` values.

### 1.6 Integrated pre-existing primitives

- `cell.hpp` — `HotCellAttribute` (4-byte AoS element; `u16 state` bit-maps a
  `BlockState`: pitch 2b, yaw 2b, then 2b brightness per face ×6, using Raylib's
  axis convention), `PackedCellAttributeCollection<CellCount, Attributes…>` (SoA,
  element `sizeof <= 8`), `SparseCellAttributeCollection<Attributes…>`. Both
  collections' `get<Attribute>()` return **references** (+ const overload).
- `block.hpp` — `BlockID = u16`, `Block`, `BlockRegistry`, `BlockBuilder`,
  `BlockRegistryBuilder`, name↔id lookup.
- `inspect.hpp` — `Inspector<T>` visitor plumbing. `types.hpp` — type aliases,
  `UnimplementedException`, `THROW_UNIMPLEMENTED()`.

### 1.7 Test harness (`tests/`, A11–A12)

`doctest` v2.5.3 via FetchContent; one `cellulose_tests` binary from
`GLOB tests/{main,test_*}.cpp CONFIGURE_DEPENDS`; `doctest_discover_tests`
registers each `TEST_CASE` with CTest. `option(CELLULOSE_BUILD_TESTS …)` defaults
ON only when `cellulose` is the top-level project. `tests/main.cpp` is the sole TU
defining the doctest main.

(doctest was pinned at v2.4.11 during the foundation build, whose
`cmake_minimum_required(3.0)` needed a scoped `CMAKE_POLICY_VERSION_MINIMUM`
shim under CMake 4; v2.5.3 declares `3.14` and configures cleanly, so the shim
was dropped.)

---

## 2. Concurrency & Thread Safety — **designed, not built**

Concurrency is managed at the **chunk level** — world-level locking causes severe
contention; per-block locking needs false-sharing padding around every block at
unacceptable memory cost. Chunk-level is the balance.

Voxel workloads are read-dominated (meshing, physics, raycasting), so lightweight
primitives rather than mutexes:

- **Sequence locks** for hot & cold data — optimistic unblocked reads, no writer
  starvation. Safe because the hot and cold arrays are fixed-size, so a
  concurrent update cannot cause an out-of-bounds error (a torn read is retried).
- **Read-write locks** for freezing-cold sparse lists — infrequent access keeps
  writer-starvation risk minimal.

False-sharing elimination:

- Chunk structures aligned to hardware cache boundaries via `alignas` /
  `std::hardware_destructive_interference_size`.
- Internal chunk locks padded to isolate lock-state synchronisation from adjacent
  voxel data.

This subsystem must also resolve C1 (see §1.5): pick a chunk container that gives
stable chunk addresses under insertion.

---

## 3. Spatial Querying — **designed, not built**

Three query types:

1. **Raycasting** — optimized 3D DDA per Amanatides & Woo, *A Fast Voxel
   Traversal Algorithm for Ray Tracing*.
2. **Volumetric queries** — retrieval of voxel data within AABBs and spherical
   regions.
3. **Collision detection** — collision normals and positional corrections for a
   target AABB + velocity vector; a baseline for custom physics integration.

---

## 4. Rendering Pipeline — **designed, not built**

- **Greedy meshing** to generate optimized mesh geometry from spatial data.
- **Level of Detail (LOD):** downsample voxel clusters into macro-blocks to
  extend effective render distance efficiently.

---

## Locked decisions — quick reference

| ID | Decision |
|----|----------|
| A1 | Chunk edge length `32` (`32³ = 32768` cells) |
| A2 | `CellIndex = u32`, `libmorton::morton3D_32_encode` |
| A3 | `LocalPosition{ u8 x,y,z }` in `[0,32)` |
| A4 | `ChunkPosition{ i32 x,y,z }`, padding-free (`sizeof == 12`) |
| A5 | `WorldPosition{ i64 x,y,z }` |
| A6 | world↔chunk split = arithmetic `>> 5` / `& 31` |
| A7 | hot storage = `std::array<HotCellAttribute, 32768>`, AoS, Morton order |
| A8 | cold/freezing collection types default to *empty*; concrete types deferred |
| A9 | `chunk()` = get-or-create; `find_chunk()` = lookup-only nullable |
| A10 | `ChunkPositionHash` = avalanching, raw-byte wyhash over the 12 bytes |
| A11 | tests = `doctest` (v2.5.3), one binary, `doctest_discover_tests` |
| A12 | `CELLULOSE_BUILD_TESTS` defaults ON only top-level |
| R1 | commits carry the two attribution trailers |
| R2 | `coordinate.hpp` independent of `chunk.hpp`; literal `5`/`31` + comment |
| R5 | `ChunkPositionHash` may use `unordered_dense::detail::wyhash` |
| C1 | chunk pointers/refs are invalidated by `World` inserts — do not retain them |
| C2 | `cellulose_tests` links raylib transitively though no test uses it |

## Known follow-ups (cross-cutting, not tied to one subsystem)

- `PackedCellAttributeCollection` requires element `sizeof <= 8`, but the README
  describes cold data as "> 8 bytes" — reconcile when a concrete cold attribute
  type is introduced.
- `HotCellAttribute::get_pitch` / `get_yaw` return values in shifted bit-space
  (consistent with the pre-shifted enum constants) — left as-is.
- Consider relocating `chunk_edge_length` or adding a coupling `static_assert`
  once the `coordinate.hpp` ↔ `chunk.hpp` dependency rule (R2) can be revisited.
- C2: consider a raylib-free core `INTERFACE` target so tests/consumers of the
  data structures don't pull the renderer.
