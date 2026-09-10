# Cellulose — Remaining Tasks

Tasks are grouped by phase — the four design concerns in `README.md`; the design
intent and locked decisions for each are in `ARCHITECTURE_SPEC.md` (§1–§4), with
per-phase plans under `docs/plans/`.

**All four phases are implemented and tested.** What remains is the Phase 1
close-out review, the cross-cutting backlog below, and merging `main` to
`origin`.

Legend: `[x]` done · `[ ]` not started · `[~]` partially done / needs follow-up.

---

## Phase 1 — Core Data Structure — **complete** (merged to `main`, `33cf989..166f51f`)

- [x] doctest harness wired into CTest (`tests/`, `CELLULOSE_BUILD_TESTS`)
- [x] `*CellAttributeCollection::get()` returns references (+ const overloads)
- [x] `HotCellAttribute` face-brightness getter operator-precedence fix
- [x] `coordinate.hpp` — `LocalPosition` / `ChunkPosition` / `WorldPosition`,
      `ChunkPositionHash`, world/chunk/local conversions
- [x] `morton.hpp` — `CellIndex`, `encode_cell_index` / `decode_cell_index`
- [x] `chunk.hpp` — `Chunk<>` (Morton hot `std::array` + packed/sparse collections)
- [x] `world.hpp` — `World<>` chunk hash table + `find_hot_attribute`
- [x] `cellulose.hpp` umbrella header, `src/main.cpp` demo, README status note
- Suite: 20/20 green.

### Phase 1 close-out (cleanups on the merged foundation)

- [x] **`world.hpp` `for_each_chunk`:** no longer re-forwards the visitor per
      iteration — calls `p_visitor(position, stored_chunk)` directly.
- [x] **`world.hpp` `using ChunkMap`:** moved into the `private` section.
- [x] **Const-overload coverage:** added a `const Chunk<>` spot-check in
      `test_chunk.cpp` exercising the `hot_attribute` const overloads and the
      `hot_attribute(CellIndex)` write path; the umbrella const-`World` case
      stays.
- [x] **doctest:** bumped `v2.4.11` → `v2.5.3` (declares
      `cmake_minimum_required(3.14)`), dropped the scoped
      `CMAKE_POLICY_VERSION_MINIMUM` shim — configure is now warning-free.
- [x] **`tests/CMakeLists.txt` glob:** narrowed from `GLOB_RECURSE *.cpp` to a
      non-recursive `main.cpp` + `test_*.cpp` list.
- [x] Cosmetic: dropped `main.cpp`'s redundant `#include <cellulose/inspect.hpp>`;
      added the README trailing newline; guarded `test_umbrella.cpp` case 1 with
      `REQUIRE`; renamed the `world.hpp` locals that shadowed `chunk()`; reworded
      the `world.hpp` doc comment.
- [ ] **Optional: a final whole-branch review** of the foundation
      (`33cf989..166f51f`). Per-task reviews were clean; a consolidated pass was
      never recorded.

Suite after close-out: **21/21** green; configure + build warning-free for
`inc/cellulose/*`. (Suite is now 70; the demo is a voxel game — history above.)

---

## Phase 2 — Concurrency & Thread Safety — **complete**

Design: `ARCHITECTURE_SPEC.md` §2. Plan (with decisions D1–D11):
`docs/plans/phase-2-concurrency.md`.

- [x] Phase 2 implementation plan.
- [x] **T1** — `Threads::Threads` link + `CELLULOSE_SANITIZER` build option
      (`/fsanitize=` on MSVC, `-fsanitize=` elsewhere; TSan is Linux/macOS only).
- [x] **T2** — `sync.hpp`: `cache_line_size` + `Padded<T>`.
- [x] **T3** — `SeqLock` primitive + torn-read stress test.
- [x] **T4** — `RWLock` primitive + grow-under-read stress test.
- [x] **T5** — `Chunk` embeds a hot/cold `SeqLock` (+ writer mutex) each and a
      sparse `RWLock`, `alignas(cache_line_size)`, padded; `read_*`/`write_*`
      functor accessors added alongside the (now unsynchronised) bare ones.
- [x] **T6** — `World` stores `unique_ptr<Chunk>` (C1 resolved) under a directory
      `std::shared_mutex`.
- [x] **T7** — `ARCHITECTURE_SPEC` §2 / §1.4 / §1.5 rewritten; suite 32/32,
      format clean, demo unchanged.

Thread-safety follow-ups since resolved (backlog below): thread-safe chunk
*unload* (opt-in `ChunkStorage::Shared`), the seqlock memory model (D3 —
`std::atomic_ref` is now the default), and world-directory sharding (D5 — done).
Still deferred: the CAS single-writer seqlock claim (D4).

---

## Phase 3 — Spatial Querying — **complete**

Design: `ARCHITECTURE_SPEC.md` §3. Plan (decisions D1–D10):
`docs/plans/phase-3-spatial-querying.md`.

- [x] Phase 3 implementation plan.
- [x] **T1** — `vector.hpp`: `Vector3<T>` (`Vec3`/`Vec3d`/`Vec3i`, `operator[]`),
      `Aabb`, `to_cell` / `to_point`.
- [x] **T2** — `raycast()` — Amanatides & Woo voxel DDA; hit cell, entry-face
      normal, distance; 200-ray fuzz cross-check.
- [x] **T3** — `for_each_cell_in_aabb` / `for_each_cell_in_sphere`.
- [x] **T4** — `move_aabb()` — axis-separated swept AABB-vs-voxel collision.
- [x] **T5** — umbrella + `ARCHITECTURE_SPEC` §3 + this file; suite 49/49.

All queries take a caller `bool(const HotCellAttribute &)` predicate and read
cells as by-value snapshots through the per-chunk seqlock. Deferrals moved to the
backlog: raycast/volume chunk caching, bulk chunk snapshots, continuous
collision, sphere/capsule casts, registry-driven solidity.

---

## Phase 4 — Rendering Pipeline — **complete**

Design: `ARCHITECTURE_SPEC.md` §4. Plan (decisions D1–D11):
`docs/plans/phase-4-rendering.md`.

- [x] Phase 4 implementation plan.
- [x] **T1** — `mesh.hpp`: `MeshVertex` / `ChunkMesh` / `MeshSample` +
      `greedy_mesh` (0fps greedy meshing, hidden-face culling,
      `(block_id, brightness)` merge keys, CCW winding).
- [x] **T2** — `mesh_chunk(world, chunk_position, is_solid)` — `34³` apron
      sampled via seqlock snapshots; cross-chunk face culling; absent chunk = air.
- [x] **T3** — `mesh_chunk_lod(world, chunk_position, level, is_solid)` —
      any-solid macro-cells, first-solid attributes, quads scaled by `1 << level`.
- [x] **T4** — raylib render demo in `src/main.cpp` (since replaced by the
      minimal voxel game: fly camera, raycast, LMB break / RMB place, per-chunk
      remesh on edit).
- [x] **T5** — umbrella + `ARCHITECTURE_SPEC` §4 + this file; suite 63/63.

Output is renderer-neutral (chunk-local `f32` vertices; raylib only in the demo).
Texture management landed later (see backlog). Remaining deferrals: AO, non-cube
blocks, transparency, incremental remesh / mesh cache, LOD seam stitching,
threaded meshing, non-uniform-tile atlas shader.

---

## Cross-cutting / backlog

Design decisions D1–D10 (from the design review) and their triggers are in
`docs/plans/design-followups.md`.

- [x] **Attribute extensibility (D1).** All three tiers are consumer-supplied.
      `Chunk<HotAttribute HotType = HotCellAttribute, Packed, Sparse>`; the
      `HotAttribute` concept requires only `block_id`; the mesher reads brightness
      through the `face_brightness` CPO. `PackedChunkAttributes` /
      `SparseChunkAttributes` aliases. The base ships **no** concrete
      cold/freezing types — by design; a subsystem that needs one supplies it.
- [x] **Reconcile cold-data size.** Tiers are chosen by access frequency; the
      enforced split is `Packed` ≤ 8 < `Sparse` (`static_assert` on each; Sparse
      tightened `>= 8` → `> 8`), documented in `ARCHITECTURE_SPEC.md` §1.4. The
      README's "> 8 bytes" for cold data is illustrative.
- [x] **C2 — raylib-free core target.** raylib is off the `libcellulose`
      INTERFACE (verified: `cellulose_tests.vcxproj` has zero raylib refs); only
      the demo executable links it.
- [x] **R2 coupling.** `coordinate.hpp` exposes `chunk_edge_length_shift` /
      `chunk_edge_length_mask`; `chunk.hpp` `static_assert`s
      `chunk_edge_length == (1 << chunk_edge_length_shift)`.
- [x] **`to_chunk_position` narrowing.** Documented: correct while
      `|axis| < 2^(31 + shift)` (≈ ±2^36 blocks).
- [x] **Thread-safe chunk unload (D2).** Opt-in `World<ChunkType, ChunkStorage::Shared>`
      stores `shared_ptr<Chunk>`; `find_chunk` returns a handle that pins the
      chunk past `remove_chunk`. `Unique` (raw ptr, caller-quiescence) stays the
      default. **Decided against** epoch / hazard-pointer reclamation for the base.
- [x] **Concurrency benchmarks.** `benchmarks/` (dependency-free harness) with
      scenarios B1–B7; plan `docs/plans/concurrency-benchmarks.md`, first results
      `docs/benchmarks/RESULTS-2026-09-10-i7-14700HX.md`. Linux CI `Benchmarks`
      workflow runs the mixed workload under TSan + ASan — **clean**. The D3/D4/D5
      verdicts below are now settled.
- [x] **`CELLULOSE_STRICT_ATOMICS` is the default now (D3 — verdict: yes).**
      `chunk.hpp` auto-defines it unless `CELLULOSE_LOOSE_ATOMICS` is set;
      CMake option renamed to `CELLULOSE_LOOSE_ATOMICS` (default OFF); the
      torn-read stress tests (`test_chunk.cpp`, `bench_seqlock`) are now
      `#ifdef CELLULOSE_LOOSE_ATOMICS`; benchmarks TSan job drops the redundant
      flag. A `write_hot` closure must assign whole elements.
- [x] **`World` directory is sharded (D5 — verdict: yes).** `world.hpp`: 16
      power-of-two hash-bit shards, each a `Padded<{ shared_mutex, sub-map }>`;
      `has_chunk` / `find_chunk` / `chunk` / `remove_chunk` touch one shard;
      `for_each_chunk` locks all 16 shared (index order), `chunk_count` sums.
      Local B4 re-run: lookups/s now scale 12M→57M over 1→16 queriers (was flat).
- [ ] **CAS single-writer seqlock (D4 — verdict: defer).** B1/B7: writer-vs-writer
      contention is real but modest (writes/s −30% from 1→4 writers; p99 to ~33 µs)
      — not urgent. The ~160 B/chunk saving from dropping the per-tier `std::mutex`
      is the better motivation. Revisit if a world-gen profile shows it hot.
- [ ] **Lighter chunk handle (from B5).** `ChunkStorage::Shared` costs ~25% on
      `find_chunk` (shared_ptr refcount). Fine as an opt-in; if streaming engines
      adopt it widely, design a generational handle (`{slot, generation}` +
      `resolve`) instead of `shared_ptr`.
- [x] **`BlockRegistryBuilder::build()` bug** (pre-existing) — fixed (reserve +
      push; ids map to the named blocks); `test_block.cpp` added. Unblocks a
      `BlockRegistry`-backed solidity predicate.
- [~] **Spatial-query performance** (from Phase 3). `raycast`, `move_aabb` and
      the mesher's apron sampler now walk cells through `impl::ChunkCursor`
      (`cursor.hpp`) — one `find_chunk` / directory-lock per chunk crossing
      instead of per cell (full-chunk LOD-5 mesh test: 0.29 s → 0.06 s).
      `for_each_cell_in_aabb` was already chunk-major. Still open: a **bulk
      per-chunk `read_hot`** for volume/mesh copying the local sub-range in one
      seqlock acquisition instead of one per cell.
- [x] **Transparency / cutout meshing (D8).** `mesh_chunk` / `mesh_chunk_lod`
      take `has_geometry(attr)` + `is_hidden(near, far)` (4-arg overload); the
      single-predicate form is kept as the opaque-cube convenience.
- [x] **Incremental-remesh signal (D9).** `Chunk::revision()` — a monotonic write
      counter. A `ChunkMeshCache` (dirty set + neighbour invalidation) may follow
      as an optional module.
- [ ] **Continuous collision / shape casts (D10) — decided, deferred.** Keep
      `move_aabb`. Add `sweep_aabb` (Minkowski time-of-impact, no resolution) and
      sphere/capsule casts **on demand**.
- [ ] **Ambient occlusion (D6) — decided.** Consumer's concern; if built into the
      mesher it is an opt-in flag with AO in the merge key, never default.
- [x] **Non-cube block shapes (D7) — won't do.** Greedy meshing is a cube
      optimisation; a block-model system belongs in the consumer's engine.
- [x] **Bitwise / SIMD greedy meshing — won't do.** The binary-greedy-meshing
      technique (~30× faster) needs a linear column-major layout so 64 cells pack
      into one `u64`. `cellulose` stores the hot array **Morton-ordered** for
      spatial-query / physics locality — the primary use case — and a de-Morton
      pass plus per-`(block_id, brightness)` masks would erode most of the win.
      The scalar mesher (~block-mesh-rs ballpark) is the deliberate balance. Not
      concurrency-limited — the mesher runs lock-free on a thread-local buffer.
- [x] **Texture management — done** (`docs/plans/texture-management.md`).
      Per-face `TextureID` on `Block` / `BlockRegistry`; `face_texture` CPO;
      `texture_id` in the greedy merge key + on every `MeshVertex`;
      `mesh_chunk` / `mesh_chunk_lod` texture-resolver overloads;
      `TextureAtlas` + `atlas_builder` (`pack` / `strip`); raylib atlas-tiling
      bridge (`to_raylib_mesh(mesh, atlas)`, `load_atlas_shader` /
      `load_atlas_material`). Non-uniform-tile atlas support in the shipped
      shader is deferred (needs per-vertex tile size).
- [ ] **Other meshing follow-ups:** a per-`ChunkPosition` `ChunkMeshCache` using
      `revision()`; LOD seam stitching (skirts between adjacent levels); run
      `mesh_chunk` on a worker pool; a bulk per-chunk `read_hot` (one seqlock
      acquire + copy instead of per-cell).
- [x] **`ChunkMesh → raylib::Mesh` bridge** moved to an opt-in
      `cellulose/raylib.hpp` (not in the umbrella; include it with raylib on the
      link line). `to_raylib_mesh(mesh, color_fn)` + a grayscale default;
      `to_raylib_mesh(mesh, atlas)` for the textured path; the demo builds a
      procedural atlas.
