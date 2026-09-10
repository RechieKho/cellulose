# Cellulose — Remaining Tasks

Tasks are grouped by phase. Phase 1 mirrors the four design concerns in
`README.md`; the design intent for each is in `ARCHITECTURE_SPEC.md` (§1–§4).

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

## Phase 2 — Concurrency & Thread Safety — **not started**

Design: `ARCHITECTURE_SPEC.md` §2. Chunk-level granularity.

- [ ] **Resolve chunk-pointer invalidation (C1) first.** `World` value-stores
      ~131 KB chunks in `unordered_dense::map`; inserts rehash and move every
      chunk. Evaluate `unordered_dense::segmented_map` or
      `unique_ptr<Chunk>` values so chunk addresses are stable — a prerequisite
      for handing chunk references to worker threads.
- [ ] **Sequence lock** primitive for the hot and cold arrays: optimistic
      unblocked reads, no writer starvation; torn reads retried. Fixed-size
      arrays make concurrent updates memory-safe.
- [ ] **Read-write lock** primitive for the freezing-cold sparse lists.
- [ ] Integrate the locks into `Chunk` (per-chunk lock state).
- [ ] **False-sharing elimination:** `alignas` chunks to
      `std::hardware_destructive_interference_size`; pad internal chunk locks to
      isolate lock state from adjacent voxel data.
- [ ] Concurrency stress tests (readers vs. writers; meshing while editing).
- [ ] Write the Phase 2 implementation plan before starting.

---

## Phase 3 — Spatial Querying — **not started**

Design: `ARCHITECTURE_SPEC.md` §3. Built on `World` / `Chunk`.

- [ ] **Raycasting** — 3D DDA per Amanatides & Woo fast voxel traversal;
      returns hit cell, face, and distance.
- [ ] **Volumetric queries** — retrieve voxel data within an AABB and within a
      sphere.
- [ ] **Collision detection** — collision normals + positional correction for a
      target AABB and a velocity vector (physics-integration baseline).
- [ ] Query tests (known scenes, analytic expected hits).
- [ ] Write the Phase 3 implementation plan before starting.

---

## Phase 4 — Rendering Pipeline — **not started**

Design: `ARCHITECTURE_SPEC.md` §4.

- [ ] **Greedy meshing** — generate optimized mesh geometry from chunk voxel
      data (consumes `HotCellAttribute` orientation/brightness bits).
- [ ] **Level of Detail** — downsample voxel clusters into macro-blocks; build
      multiple LODs to extend effective render distance.
- [ ] Mesh-correctness tests (face count / winding / no cracks between LODs).
- [ ] Wire a real render demo in `src/main.cpp` (raylib) replacing the stdout demo.
- [ ] Write the Phase 4 implementation plan before starting.

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
