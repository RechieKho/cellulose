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
`inc/cellulose/*`; demo prints `chunks loaded: 1` / `block at (1,2,3): 42`.

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

Remaining thread-safety follow-ups moved to the backlog below: thread-safe chunk
*unload*, world-directory sharding, CAS single-writer seqlock claim, and the
seqlock memory-model question (benign-race hot path vs. `std::atomic_ref`, for a
TSan-clean Linux build).

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
- [x] **T4** — raylib render demo in `src/main.cpp` (ChunkMesh → `Mesh` →
      `DrawModel` under an orbital camera; prints vertex / triangle counts).
- [x] **T5** — umbrella + `ARCHITECTURE_SPEC` §4 + this file; suite 63/63.

Output is renderer-neutral (chunk-local `f32` vertices; raylib only in the demo).
Deferrals moved to the backlog: AO, atlas UVs, non-cube blocks, transparency,
incremental remesh / mesh cache, LOD seam stitching, threaded meshing, apron
sampler chunk-pointer caching.

---

## Cross-cutting / backlog

- [ ] **Concrete cold & freezing attribute types.** `Chunk`'s packed/sparse
      collections default to empty (A8). Lighting introduces a cold attribute;
      tile entities introduce a freezing-cold one. Introduce them with their
      owning subsystem.
- [ ] **Reconcile cold-data size.** `PackedCellAttributeCollection` requires
      element `sizeof <= 8`; README describes cold data as "> 8 bytes". Resolve
      when the first concrete cold type lands.
- [ ] **C2 — raylib-free core target.** `cellulose_tests` links raylib
      transitively via the INTERFACE lib though no test uses it. Consider a
      renderer-free core `INTERFACE` target.
- [ ] **R2 coupling.** `coordinate.hpp` hardcodes `5` / `31` to avoid depending
      on `chunk.hpp`'s `chunk_edge_length`. If the edge length is ever made
      configurable, relocate the constant or add a coupling `static_assert`.
- [ ] **`to_chunk_position` narrowing.** Unguarded `i64 -> i32` on the chunk
      axis — document or assert the effective world-size boundary.
- [ ] **Thread-safe chunk unload** (deferred from Phase 2). Reclaiming a `Chunk`
      while worker threads may hold a `Chunk*` needs `shared_ptr` / hazard
      pointers / epoch reclamation. Until then `remove_chunk` requires the caller
      to guarantee quiescence.
- [ ] **World directory scaling** (deferred from Phase 2). If the single
      `shared_mutex` over the chunk map contends under many loader threads, shard
      by hash bits or move to a concurrent map. Measure first.
- [ ] **Seqlock memory model** (deferred from Phase 2). `Chunk::read_hot` /
      `read_cold` read the plain arrays under the seqlock — a benign race that
      ThreadSanitizer will flag. For a TSan-clean Linux build, switch the hot/cold
      element access to `std::atomic_ref` and benchmark the cost.
- [ ] **CAS single-writer seqlock** (deferred from Phase 2). Replace the per-tier
      writer `std::mutex` with a CAS-claimed writer slot if profiling shows it hot.
- [ ] **`BlockRegistryBuilder::build()` bug** (pre-existing). `build()` pre-sizes
      `Store` to N default blocks then `push_back`s N more, and fills
      `name_id_map` with indices 0..N-1 while the real blocks land at N..2N-1.
      Blocks a `BlockRegistry`-backed solidity predicate for spatial queries.
- [ ] **Spatial-query performance** (deferred from Phase 3). `raycast` and the
      `for_each_cell_in_*` queries do a `find_chunk` (+ directory shared-lock)
      per cell. Cache the current `Chunk*` and re-resolve only when crossing a
      chunk boundary; for volume queries, one bulk `read_hot` per chunk copying
      the local sub-range.
- [ ] **Continuous collision** (deferred from Phase 3). `move_aabb` is exact per
      axis but resolves axes in a fixed X→Y→Z order; add conservative
      advancement / a swept test for fast diagonal movers if needed.
- [ ] **Sphere / capsule casts** (deferred from Phase 3).
- [ ] **Meshing follow-ups** (deferred from Phase 4): per-vertex ambient
      occlusion (part of the merge key, or it breaks merging); texture-atlas UVs
      (`block_id → atlas rect`, needs `Block` texture data); non-cube block shapes
      from `HotCellAttribute` pitch/yaw; a transparent / cutout pass; dirty-flag
      incremental remesh + a per-`ChunkPosition` mesh cache; LOD seam stitching
      (skirts / transition cells between adjacent levels); run `mesh_chunk` on a
      worker pool; cache the ≤27 chunk pointers the apron sampler touches instead
      of a `find_chunk` per cell.
- [ ] **Move the demo's `ChunkMesh → raylib::Mesh` bridge** into an optional
      `cellulose/raylib.hpp` (guarded, opt-in) if consumers want it — currently
      it lives only in `src/main.cpp`.
