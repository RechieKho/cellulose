# Cellulose — Architecture Specification

`cellulose` is a performant C++ voxel library providing spatial data structures
and operations for Minecraft-style voxel engines under concurrent settings.

This document is the self-contained architecture reference. It folds together the
design narrative from `README.md`, the locked parameters and rulings established
while building the foundation, and the as-built public surface. Forward work is
tracked in `REMAINING_TASKS.md`.

- **Language / build:** C++20, CMake ≥ 3.10. `libcellulose` is a header-only
  `INTERFACE` target; all library code lives in headers under `inc/cellulose/`.
- **Deps:** `ankerl::unordered_dense` + `fmt` (submodules), `raylib` (FetchContent,
  demo only), `doctest` v2.5.3 (FetchContent, tests only). Morton coding is
  hand-rolled in `morton.hpp` — no `libmorton` (its `uint_fast*`-templated LUT
  overloads are ambiguous on LP64 where those types collapse).
- **Status:** all four subsystems implemented and tested (70 cases).

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
- `encode_cell_index(LocalPosition) -> CellIndex` / `decode_cell_index` — a
  self-contained 3-D magic-bits interleave (`x` → bit 0, `y` → 1, `z` → 2),
  well-defined for each axis in `[0, 1024)`.

Over the 5-bit-per-axis chunk domain this is a bijection onto `[0, 2^15)`
(verified exhaustively for all 32768 positions).

### 1.4 Chunk (`chunk.hpp`)

- `inline constexpr size chunk_edge_length = 32;` / `chunk_cell_count = 32768;`
- `Chunk<HotAttribute HotType = HotCellAttribute,
   PackedCollection = PackedCellAttributeCollection<chunk_cell_count>,
   SparseCollection = SparseCellAttributeCollection<>>` — `final`,
  default-constructible, non-movable.

**All three attribute tiers are consumer-supplied.** The base library ships one
reference type (`HotCellAttribute`) and no cold/freezing types — it has no need
for them. A consumer plugs in their own via the template parameters;
`PackedChunkAttributes<T…>` / `SparseChunkAttributes<T…>` are the ergonomic
spellings for the cold / freezing packs.

**`HotAttribute` concept** (`cell.hpp`) — the only requirements on a hot type:
`std::is_trivially_copyable_v` (the seqlock snapshots it by value),
`std::default_initializable` (fresh chunks zero-fill), and a `block_id` member
convertible to `BlockID` (every query answers "what block did I hit"). Brightness
/ orientation are **not** required — the mesher reads them through the
`face_brightness(attr, face)` customization point (defaulted for `HotCellAttribute`,
flat-shaded when a custom type provides no overload).

| Member (+ const overload) | Purpose |
|---------------------------|---------|
| `read_hot(fn) const` / `write_hot(fn)` | run `fn` over the hot `std::array<HotType>` under the hot seqlock — **the concurrent path** |
| `read_cold(fn) const` / `write_cold(fn)` | run `fn` over the packed collection under the cold seqlock |
| `read_sparse(fn) const` / `write_sparse(fn)` | run `fn` over the sparse collection under the sparse `RWLock` |
| `revision() const -> u64` | monotonic change counter, bumped by every `write_*` (conservative — bumps on no-op writes); a dirty signal for meshing / networking / persistence |
| `hot_attribute(LocalPosition\|CellIndex) -> HotType&` | address one cell — **unsynchronised**, single-threaded / caller-locked only |
| `fill_hot(const HotType&)`, `packed()`, `sparse()` | unsynchronised direct access |

`Chunk` is `alignas(cache_line_size)`; each lock group and the revision counter
sit on their own cache line, clear of the voxel arrays and of each other (§2). A
seqlock read functor must return a snapshot **by value** — a torn snapshot
mid-write is discarded and the read retried, sound only over the fixed-size arrays.

**Hot-tier atomicity (D3).** By default `read_hot` / `write_hot` reach the hot
array through `std::atomic_ref` (relaxed loads/stores) — no data race,
ThreadSanitizer-clean, ≈free on the pure-read (meshing) path (benchmark B2). A
`write_hot` closure therefore assigns **whole elements** (`hot[i] = value`), not
fields. `-DCELLULOSE_LOOSE_ATOMICS=ON` (macro `CELLULOSE_LOOSE_ATOMICS`) opts
back into the plain-array benign race: ~30% faster writes, field assignment
allowed, but UB by the standard and TSan-flagged.

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

Both collection parameters default to the *empty* collection so `Chunk<>` is
zero-overhead.

**Name-lookup gotcha:** inside `namespace cellulose::impl`, unqualified
`SeqLock` / `RWLock` / `Padded` / `HotCellAttribute` bind to the unspecialised
templates, not the `cellulose::` aliases — `impl::` code qualifies them.

### 1.5 World (`world.hpp`)

`World<ChunkType = Chunk<>, ChunkStorage Storage = ChunkStorage::Unique>` over
`ankerl::unordered_dense::map<ChunkPosition, StoredChunk, ChunkPositionHash>`
(`StoredChunk` = `unique_ptr` / `shared_ptr` by `Storage`), `final`, non-movable.
A `std::shared_mutex` guards the directory.

| Member | Semantics |
|--------|-----------|
| `has_chunk(ChunkPosition) const -> bool` | shared directory lock |
| `find_chunk(ChunkPosition) -> ChunkHandle` (+ const) | `nullptr` when absent; shared lock. `ChunkHandle` = raw `ChunkType*` under `Unique` (valid across other chunks' inserts/erases), `shared_ptr<ChunkType>` under `Shared` (pins the chunk) |
| `chunk(ChunkPosition) -> ChunkType&` | **get-or-create**; exclusive lock |
| `remove_chunk(ChunkPosition) -> bool` | `true` if erased; exclusive lock. Under `Unique`, **the caller must ensure no other thread is using that chunk**; under `Shared`, outstanding handles keep it alive |
| `chunk_count() const -> size` | shared lock |
| `for_each_chunk(Visitor&&)` | shared lock for the whole traversal; `visitor(const ChunkPosition&, ChunkType&)` |
| `find_hot_attribute(WorldPosition) -> HotCellAttribute*` (+ const) | resolve through the containing chunk; `nullptr` when absent — **unsynchronised deref**, single-threaded / caller-locked |

**C1 — resolved (Phase 2).** `unique_ptr` storage keeps a `ChunkType*` valid
across other chunks' inserts/erases. Thread-safe *unload while a reader holds a
pointer* is handled by opting into `ChunkStorage::Shared` (handles pin the
chunk); the default `Unique` keeps the caller-quiescence contract on
`remove_chunk`. No epoch / hazard-pointer machinery — see `docs/plans/design-followups.md`.

### 1.6 Integrated pre-existing primitives

- `cell.hpp` — `HotCellAttribute` (the reference hot type: 4-byte AoS element;
  `u16 state` bit-maps pitch 2b, yaw 2b, then 2b brightness per face ×6, Raylib's
  axis convention), the `HotAttribute` concept, the `face_brightness` CPO,
  `PackedCellAttributeCollection<CellCount, Attributes…>` (SoA, `sizeof <= 8`),
  `SparseCellAttributeCollection<Attributes…>` (per-cell map, `sizeof > 8`). Both
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
| `seqlock.hpp` | `SeqLock` | hot tier, cold (packed) tier — one each (`impl::TierLock` = seqlock + writer `std::mutex`) | atomic sequence counter, odd→write→even; readers snapshot-and-retry, never block; writers never starved. Read functor must return **by value**; torn snapshots are discarded. Sound only over the fixed-size arrays. `write()` is not writer-vs-writer safe — the paired `std::mutex` serialises writers. Hot-tier element access defaults to `std::atomic_ref` (D3, above); `CELLULOSE_LOOSE_ATOMICS` reverts to the benign race. |
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
in the core. `cellulose/raylib.hpp` is an **opt-in** bridge (not in the umbrella;
include it with raylib linked): `to_raylib_mesh(ChunkMesh, color_fn) -> Mesh`.

**Output types:** `MeshVertex{ Vec3 position; Vec3 normal; f32 u, v; f32 brightness; u32 block_id; TextureID texture_id; }`
(positions chunk-local in `[0, chunk_edge_length]`; `u`/`v` are tile-space, `[0, w]×[0, h]`;
`texture_id` is the render key — see §4.1);
`ChunkMesh{ vertices, indices }` (index triples, CCW-front).

| Function (`namespace cellulose`) | Behaviour |
|----------------------------------|-----------|
| `greedy_mesh(MeshSample grid, size, block_scale) -> ChunkMesh` | The 0fps greedy mesher on a `size³` grid of `MeshSample{ visible[6], block_id, brightness[6] }`: per face direction and slice, build a `(block_id << 8 \| brightness) + 1` key mask over the cells whose face is `visible`, merge maximal rectangles, emit one CCW quad each scaled by `block_scale`. |
| `mesh_chunk(world, cp, is_solid) -> ChunkMesh` | Opaque-cube convenience — `has_geometry = is_solid`, `is_hidden(near, far) = is_solid(far)`. |
| `mesh_chunk(world, cp, has_geometry, is_hidden) -> ChunkMesh` | General form: `has_geometry(attr)` decides whether a cell emits faces, `is_hidden(near, far)` decides face culling — for transparency / cutout (e.g. water-vs-water hidden, water-vs-glass not). |
| `mesh_chunk_lod(world, cp, level, …)` | `level` 0–5 (3-arg and 4-arg rule forms): merges each `(1 << level)³` block into one macro-cell (geometry if any cell has it, attributes from the first) then greedy-meshes the `32 >> level` grid with `block_scale = 1 << level`. `level 0` ≡ `mesh_chunk`. |

`impl::sample_chunk` builds the `n³` grid from `Chunk::read_hot` snapshots (an
absent neighbour chunk is a default-constructed hot attribute), computing each
`MeshSample::visible[face] = has_geometry(cell) && !is_hidden(cell, neighbour)` —
so the culling decision lives where the attributes are, and `greedy_mesh` never
looks at neighbours.

**Merge key** = `(that-face's texture id, that-face's brightness)` — the texture
id (`§4.1`), **not** `block_id`, so two blocks that share a face texture merge
and one block whose faces differ does not. Brightness from the `face_brightness`
CPO (flat for a hot type without it). Cubes only — `HotCellAttribute` pitch/yaw
orientation bits are unused. `block_id` on the emitted vertices is the merged
run's origin cell.

### 4.1 Texture management

Consumer-driven, renderer-neutral. Nothing in the core decodes an image.

| Piece (`namespace cellulose`) | Role |
|-------------------------------|------|
| `TextureID` (`= u32`), `FaceTextures{ array<TextureID,6> }` (`block.hpp`) | Per-face texture ids in canonical face order (`+X -X +Y -Y +Z -Z`). `FaceTextures::uniform(id)` / `::column(top, side, bottom)`. `0` = "unset". |
| `Block::textures`; `BlockBuilder::texture` / `texture_all` / `texture_column` | Assignment lives with the block definition. |
| `BlockRegistry::face_texture(block, face) -> TextureID`; `BlockRegistry::operator()(attr, face)` | Lookup + a ready-made mesher resolver (pass the registry as `texture_of`). |
| `face_texture(const HotType&, i32)` CPO (`cell.hpp`) | Mesher default when no resolver is passed — `HotCellAttribute` → `block_id`; overload for a custom hot type. |
| `mesh_chunk(world, cp, is_solid, texture_of)` / `mesh_chunk(world, cp, has_geometry, is_hidden, texture_of)` (+ `_lod`) | Texture-aware entry points. The 4-arg `is_solid + texture_of` form is told apart from `has_geometry + is_hidden` by `impl::FaceTextureResolver` (a resolver returns exactly `TextureID` from `(attr, i32)`). |
| `UvRect`, `TextureAtlas` (`grid(cols, rows)` / `layers(n)` / explicit `set_rect`) (`texture.hpp`) | `TextureID -> UvRect` map. Sampled by the bridge, never by the mesher. |
| `TileSize`, `PackedAtlas`, `pack(span<TileSize>)`, `strip(ids, tile_px)` (`atlas_builder.hpp`) | Size-only shelf packer → a `TextureAtlas` + sheet dimensions. `strip` is the 1×N uniform-grid convenience (`max(id)+1` rows). Consumer blits pixels into the rects. |

**raylib bridge** (`raylib.hpp`, opt-in): `to_raylib_mesh(mesh, atlas)` bakes
each vertex's tile origin (`atlas.rect_of(texture_id).min`) into `texcoords2`;
`atlas_tiling_vs` / `atlas_tiling_fs` (GLSL 330) do
`uv = origin + fract(tileUV) * uTileSize`; `load_atlas_shader(atlas)` sets
`uTileSize` from the grid; `load_atlas_material(shader, texture)` returns a stock
`Material`. One 2-D texture, one draw call, greedy merging intact — **no
`GL_TEXTURE_2D_ARRAY`, no `rlgl`, no platform `#ifdef`**. The shipped shader
assumes a uniform-grid atlas; non-uniform packed sheets need a custom shader
(per-vertex tile size). `NEAREST` filter + `CLAMP` wrap; linear/mipmapped
sampling needs a half-texel inset + `textureGrad`.

The mesher is **scalar by design**. Binary-greedy-meshing (packing 64 cells into
a `u64` for bit-parallel culling/merging, ~30× faster) needs a linear
column-major layout; the hot array is **Morton-ordered** for spatial-query /
physics locality, which is the primary workload. Trading that for a faster mesher
is the wrong balance for this library.

The apron sampler (and `raycast` / `move_aabb`) walk cells through
`impl::ChunkCursor` (`cursor.hpp`), which caches the current chunk pointer so a
run of same-chunk cells costs one `find_chunk` / directory-lock, not one per cell.

**Deferred:** ambient occlusion; non-cube block shapes (orientation bits);
transparent / cutout pass; incremental remesh + per-chunk mesh cache; LOD seam
stitching; non-uniform-tile atlas support in the shipped shader; threaded meshing
(the mesher already only needs seqlock read access); a bulk per-chunk `read_hot`
copying a local sub-range in one seqlock acquisition.

---

## Locked decisions — quick reference

| ID | Decision |
|----|----------|
| A1 | Chunk edge length `32` (`32³ = 32768` cells) |
| A2 | `CellIndex = u32`; hand-rolled magic-bits Morton3D (no `libmorton`) |
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
| §3 | queries take a `bool(HotType)` predicate; no built-in solidity; cells read as seqlock snapshots |
| §3 | `raycast` = Amanatides & Woo DDA; `move_aabb` = axis-separated swept "collide and slide" |
| §3 | `vector.hpp` (`Vector3<T>`, `Aabb`) is raylib-free |
| §4 | mesher output is renderer-neutral (`MeshVertex` / `ChunkMesh`); the raylib bridge is the opt-in `cellulose/raylib.hpp` (not in the umbrella), used only by the demo |
| §4 | greedy meshing with `(face-texture-id, face-brightness)` merge keys + hidden-face culling; cubes only (orientation bits unused) |
| §4 | LOD = any-solid macro-cells, first-solid attributes, quads scaled by `1 << level` |
| §4.1 | textures are consumer-driven: `TextureID` per face on `Block`/`BlockRegistry`, `face_texture` CPO default → `block_id`, `texture_id` in the merge key + on every vertex |
| §4.1 | atlas rendering = one 2-D sheet + `origin + fract(uv)*tileSize` tiling shader (`raylib.hpp`); `GL_TEXTURE_2D_ARRAY` rejected — no raylib API, forced per-platform raw GL |
| FD1 | all three attribute tiers are consumer-supplied; `HotAttribute` concept requires only `block_id`; brightness via the `face_brightness` CPO |
| FD2 | thread-safe unload = opt-in `ChunkStorage::Shared` (`shared_ptr` handles); no epoch/hazard machinery |
| FD8 | mesher takes `has_geometry` + `is_hidden` (transparency); single-predicate `mesh_chunk` kept as the opaque convenience |
| FD9 | `Chunk::revision()` — monotonic write counter, a dirty signal |

Adopted from the benchmark verdicts: strict `atomic_ref` hot tier as the default
(D3); a sharded `World` directory (D5, §1.5). Deferred with triggers (see
`docs/plans/design-followups.md`): CAS single-writer seqlock (world-gen profile),
a generational chunk handle (if `Shared` is adopted widely), AO (opt-in feature),
continuous collision / shape casts (on demand). Won't do: a block-model system
for non-cube shapes (engine territory).

## Known follow-ups (cross-cutting, not tied to one subsystem)

- `HotCellAttribute::get_pitch` / `get_yaw` return values in shifted bit-space
  (consistent with the pre-shifted enum constants) — left as-is.
- The attribute-tier split is now `Packed` ≤ 8 < `Sparse` (see §1.4); if a
  concrete cold attribute genuinely needs 8–16 bytes densely, revisit whether
  `Packed` should allow it or the type should be split.

See `REMAINING_TASKS.md` for the live backlog.
