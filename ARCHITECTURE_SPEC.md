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
| `read_hot(fn) const` / `write_hot(fn)` | run `fn` over the hot `std::array` under the hot seqlock — **the concurrent path** |
| `read_cold(fn) const` / `write_cold(fn)` | run `fn` over the packed collection under the cold seqlock |
| `read_sparse(fn) const` / `write_sparse(fn)` | run `fn` over the sparse collection under the sparse `RWLock` |
| `hot_attribute(LocalPosition\|CellIndex) -> HotCellAttribute&` | address one cell — **unsynchronised**, single-threaded / caller-locked only |
| `fill_hot(const HotCellAttribute&)` | set every cell — unsynchronised |
| `packed() -> PackedCollection&` / `sparse() -> SparseCollection&` | direct collection access — unsynchronised |

`Chunk` is `alignas(cache_line_size)` and non-movable (it embeds `std::mutex` /
`std::shared_mutex`); each of its three lock groups sits on its own cache line,
clear of the voxel arrays and of each other (§2). A seqlock read functor must
return a snapshot **by value** — a torn snapshot mid-write is discarded and the
read retried, which is only sound over the fixed-size hot/cold arrays.

Storage tiers (from `README.md`, keyed by access frequency for 64-byte L1 line
utilization):

| Tier | Access | Element | Layout | Backing |
|------|--------|---------|--------|---------|
| **Hot** | every frame (meshing flags) | ≤ 4 bytes | AoS, Morton-ordered | `std::array<HotCellAttribute, 32768>` inline (~131 KB) |
| **Cold** | often, not per-frame (lighting, fluid level) | ≤ 8 bytes | SoA, one dense `chunk_cell_count` array per field | `packed()` collection |
| **Freezing cold** | rare (tile entities) | > 8 bytes, or present on few cells | per-cell hash map | `sparse()` collection |

The tier is chosen by **access frequency**; the size bounds are the mechanical
consequence — a `Packed` field always costs `chunk_cell_count * sizeof(element)`
(so elements must be small), while `Sparse` only pays for cells that carry the
attribute. `README.md`'s "> 8 bytes" for cold data is illustrative; the enforced
split is `Packed` ≤ 8 < `Sparse` (a `static_assert` on each).

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
`ankerl::unordered_dense::map<ChunkPosition, std::unique_ptr<ChunkType>, ChunkPositionHash>`,
`final`, non-movable:

| Member | Semantics |
|--------|-----------|
| `has_chunk(ChunkPosition) const -> bool` | shared directory lock |
| `find_chunk(ChunkPosition) -> ChunkType*` (+ const) | lookup-only, `nullptr` when absent; shared lock; returned pointer stays valid across later inserts/erases |
| `chunk(ChunkPosition) -> ChunkType&` | **get-or-create** (`make_unique` on miss); exclusive lock |
| `remove_chunk(ChunkPosition) -> bool` | `true` if erased; exclusive lock; **caller must ensure no other thread is using that chunk** |
| `chunk_count() const -> size` | shared lock |
| `for_each_chunk(Visitor&&)` | shared lock for the whole traversal; `visitor(const ChunkPosition&, ChunkType&)` |
| `find_hot_attribute(WorldPosition) -> HotCellAttribute*` (+ const) | resolve through the containing chunk; `nullptr` when absent — **unsynchronised deref**, single-threaded / caller-locked |

**C1 — resolved (Phase 2).** Chunks are held behind `std::unique_ptr`, so a
`ChunkType*` from `find_chunk` / `chunk` stays valid across later directory
inserts **and** erases — only the pointer slot in the table moves. A
`std::shared_mutex` guards the directory (the table); per-chunk voxel data has
its own locks (§2, §1.4). Thread-safe *unload while readers hold a pointer* is
still deferred — hence the `remove_chunk` contract above.

### 1.6 Integrated pre-existing primitives

- `cell.hpp` — `HotCellAttribute` (4-byte AoS element; `u16 state` bit-maps a
  `BlockState`: pitch 2b, yaw 2b, then 2b brightness per face ×6, using Raylib's
  axis convention), `PackedCellAttributeCollection<CellCount, Attributes…>` (SoA,
  element `sizeof <= 8`), `SparseCellAttributeCollection<Attributes…>` (per-cell
  map, element `sizeof > 8`). Both
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

## 2. Concurrency & Thread Safety — **implemented** (plan: `docs/plans/phase-2-concurrency.md`)

Concurrency is at the **chunk level** — world-level locking of voxel data causes
severe contention; per-block locking needs false-sharing padding around every
block at unacceptable memory cost.

**Two lock layers, kept distinct:**

- **The world directory** — `std::shared_mutex` in `World` over the chunk *table*
  (pointer slots only). Shared for lookups / iteration, exclusive for load /
  unload. Brief and O(1), so contention stays low; this is not "world-level
  voxel locking".
- **Per-chunk voxel data** — three independent locks inside each `Chunk`, one per
  storage tier.

**Primitives** (`inc/cellulose/`):

| Header | Type | Used for | Mechanism |
|--------|------|----------|-----------|
| `seqlock.hpp` | `SeqLock` | hot tier, cold (packed) tier — one each (`impl::TierLock` = seqlock + writer `std::mutex`) | atomic sequence counter, odd→write→even; readers snapshot-and-retry, never block; writers never starved. Read functor must return **by value**; torn snapshots are discarded. Sound only over the fixed-size arrays. `write()` is not writer-vs-writer safe — the paired `std::mutex` serialises writers. |
| `rwlock.hpp` | `RWLock` | freezing-cold (sparse) tier | `std::shared_mutex` wrapper; exclusive writes, because the sparse map may reallocate. |
| `sync.hpp` | `cache_line_size`, `Padded<T>` | false-sharing elimination | `Padded<T>` over-aligns a value to a full cache line. |

**False-sharing elimination:** `Chunk` is `alignas(cache_line_size)`; each lock
group is a `Padded<…>` member, so no two locks — and no lock and the voxel
arrays — share a line.

**Concurrent API:** `Chunk::{read,write}_{hot,cold,sparse}(fn)` run `fn` under the
matching tier lock (§1.4). `World`'s directory methods take the directory lock
internally. `World::find_hot_attribute` and `Chunk`'s bare accessors stay
**unsynchronised** — single-threaded or caller-locked use only.

**Deferred:** reclaiming a chunk while another thread holds a `Chunk*` (thread-safe
unload); sharding / lock-free world directory; a CAS single-writer seqlock claim
in place of the writer mutex. See the plan and `REMAINING_TASKS.md`.

**Sanitizers:** `-DCELLULOSE_SANITIZER=<thread|address|undefined>` instruments
`cellulose_tests`. ThreadSanitizer is Linux/macOS + GCC/Clang only; the dev
machine's MSVC toolchain gets `address`. The threaded stress tests are written to
fail (assert/crash) if their synchronisation is removed, which is the portable
signal.

---

## 3. Spatial Querying — **implemented** (plan: `docs/plans/phase-3-spatial-querying.md`)

**Math (`vector.hpp`):** `Vector3<T>` value type (`Vec3` = `f32`, `Vec3d` = `f64`,
`Vec3i` = `i32`) with arithmetic, `operator[]` axis access, and free
`dot` / `cross` / `length` / `normalized` / `component_min/max`. `Aabb{ min, max }`
(f64) with `center` / `contains` / `intersects`. `to_cell(Vec3d) -> WorldPosition`
(floor) and `to_point(WorldPosition) -> Vec3d`. No raylib dependency.

**Solidity predicate.** Every query takes `bool(const HotCellAttribute &)` — there
is no built-in "solid" concept (`Block` only carries a name). A cell in an absent
chunk is empty. Each cell is read as a **by-value snapshot under the chunk's hot
seqlock**; predicates / visitors run outside that lock.

| Query (`namespace cellulose`) | Behaviour |
|-------------------------------|-----------|
| `raycast(world, Ray{origin,direction}, max_distance, is_solid) -> optional<RaycastHit>` (`raycast.hpp`) | Amanatides & Woo 3-D DDA. `RaycastHit{ cell, normal (Vec3i entry face), distance }`. Ray starting inside a solid ⇒ that cell, `normal {0,0,0}`, `distance 0`. |
| `for_each_cell_in_aabb(world, Aabb, visitor)` (`volume.hpp`) | `visitor(const WorldPosition&, const HotCellAttribute&)` for every existing cell overlapping the box; chunk-major, absent chunks skipped. |
| `for_each_cell_in_sphere(world, center, radius, visitor)` | as above, restricted by a closest-point radius test (no corner clipping). |
| `move_aabb(world, Aabb, velocity, is_solid) -> CollisionMove` (`collision.hpp`) | Axis-separated swept resolution (X, then Y, then Z): snap flush to the nearest solid per axis. `CollisionMove{ position (new box.min), normal (Vec3i, one component per blocked axis), collided }`. Exact for axis-aligned motion of any length; diagonal corner-order is approximate. |

**Deferred:** raycast/volume chunk-pointer caching (re-resolve only on a chunk
crossing); bulk whole-chunk snapshots for volume queries; continuous collision
for very fast movers; sphere/capsule casts; a `BlockRegistry`-driven default
solidity predicate (needs `Block` to gain a solidity flag).

---

## 4. Rendering Pipeline — **implemented** (plan: `docs/plans/phase-4-rendering.md`)

Renderer-neutral triangle geometry from chunk voxel data (`mesh.hpp`). No raylib
in the core — the demo (`src/main.cpp`) does the `ChunkMesh → raylib::Mesh` bridge.

**Output types:** `MeshVertex{ Vec3 position; Vec3 normal; f32 u, v; f32 brightness; u32 block_id; }`
(positions chunk-local in `[0, chunk_edge_length]`; `u`/`v` are tile-space, `[0, w]×[0, h]`);
`ChunkMesh{ vertices, indices }` (index triples, CCW-front).

| Function (`namespace cellulose`) | Behaviour |
|----------------------------------|-----------|
| `greedy_mesh(MeshSample apron grid, size, block_scale) -> ChunkMesh` | The 0fps greedy mesher on a pre-sampled `(size+2)³` grid: per face direction and slice, build a `(block_id << 8 \| brightness) + 1` key mask (a face is skipped where its neighbour is solid), merge maximal rectangles, emit one quad each, scaled by `block_scale`. |
| `mesh_chunk(world, chunk_position, is_solid) -> ChunkMesh` | Samples the `34³` apron via `Chunk::read_hot` snapshots (absent neighbour chunk ⇒ empty ⇒ boundary face kept), then `greedy_mesh(…, 32, 1)`. |
| `mesh_chunk_lod(world, chunk_position, level, is_solid) -> ChunkMesh` | `level` 0–5: merges each `(1 << level)³` cell block into one macro-cell — **solid if any** cell is, attributes from the **first solid** cell — then greedy-meshes the `32 >> level` grid with `block_scale = 1 << level` (still spans `[0, 32]`). `level 0` ≡ `mesh_chunk`. |

**Merge key** = `(block_id, that-face's 2-bit brightness)` — brightness
differences stay visible; `HotCellAttribute` pitch/yaw orientation bits are not
consumed yet (cubes only).

**Deferred:** ambient occlusion; texture-atlas UV mapping (`block_id → atlas
rect`); non-cube block shapes (orientation bits); transparent / cutout pass;
incremental remesh + per-chunk mesh cache; LOD seam stitching; threaded meshing
(the mesher already only needs seqlock read access). Also: the apron sampler does
one `find_chunk` per cell — cache the ≤27 touched chunk pointers.

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
| C1 | **resolved** — `World` stores `unique_ptr<Chunk>`; `Chunk*` is stable across insert + erase |
| C2 | **resolved** — raylib is off the `libcellulose` INTERFACE; only the demo executable links it |
| D1 | `World` chunk storage = `unordered_dense::map<…, unique_ptr<Chunk>>` (not `segmented_map` — that is not erase-stable) |
| D2 | a `std::shared_mutex` guards the `World` directory (table), separate from per-chunk locks |
| D3–D5 | hot & cold tiers each get a `SeqLock` + a writer `std::mutex`; read functor returns by value |
| D6 | sparse tier gets an `RWLock` (`std::shared_mutex`) — reallocating storage needs exclusive writes |
| D7 | functor accessors added **alongside** the bare ones; bare ones stay, unsynchronised |
| D8–D9 | `cache_line_size` constant; `Chunk` + each lock `alignas`-padded to it |
| D11 | `remove_chunk` requires caller-guaranteed quiescence (safe unload deferred) |
| §3 | queries take a `bool(HotCellAttribute)` predicate; no built-in solidity; cells read as seqlock snapshots |
| §3 | `raycast` = Amanatides & Woo DDA; `move_aabb` = axis-separated swept "collide and slide" |
| §3 | `vector.hpp` (`Vector3<T>`, `Aabb`) is raylib-free |
| §4 | mesher output is renderer-neutral (`MeshVertex` / `ChunkMesh`); raylib bridge lives only in the demo |
| §4 | greedy meshing with `(block_id, face-brightness)` merge keys + hidden-face culling; cubes only (orientation bits unused) |
| §4 | LOD = any-solid macro-cells, first-solid attributes, quads scaled by `1 << level` |

## Known follow-ups (cross-cutting, not tied to one subsystem)

- `HotCellAttribute::get_pitch` / `get_yaw` return values in shifted bit-space
  (consistent with the pre-shifted enum constants) — left as-is.
- The attribute-tier split is now `Packed` ≤ 8 < `Sparse` (see §1.4); if a
  concrete cold attribute genuinely needs 8–16 bytes densely, revisit whether
  `Packed` should allow it or the type should be split.

See `REMAINING_TASKS.md` for the live backlog.
