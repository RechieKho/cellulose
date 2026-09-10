# Phase 4 — Rendering Pipeline — Implementation Plan

**Goal:** Turn chunk voxel data into optimized triangle geometry: **greedy
meshing** (merge coplanar same-attribute faces into large quads, cull hidden
faces) and **level of detail** (mesh a chunk at a coarser block size to cut
triangle count at distance). The mesher output is renderer-neutral; a small
raylib bridge in the demo proves it draws.

**Spec:** `ARCHITECTURE_SPEC.md` §4 (from `README.md` § "Rendering Pipeline").
This plan fills in every concrete decision — correct any before/while executing.

**Scope in:** `MeshVertex` / `ChunkMesh` types; `mesh_chunk()` greedy mesher with
cross-chunk face culling and per-face-attribute merge keys; `mesh_chunk_lod()`
macro-block meshing; a raylib demo that meshes a hand-built world and renders it.

**Scope out (deferred — see end):** ambient occlusion; texture-atlas UV mapping
(the mesher emits tile-space UVs, the consumer maps `block_id` → atlas);
per-block mesh shapes / non-cube blocks (uses `HotCellAttribute` orientation
bits); transparent / cutout passes; mesh caching + incremental remesh on edit;
GPU-side meshing; seam-stitching between adjacent LOD levels.

---

## Context

- After Phase 3: `World` / `Chunk` with the concurrent seqlock accessors;
  `vector.hpp` (`Vec3`, `Vec3i`); `coordinate.hpp`; `morton.hpp`; a
  `bool(const HotCellAttribute &)` predicate convention for "solid".
- `HotCellAttribute` carries `block_id` (`u16`) and six 2-bit face-brightness
  values (`get_right/left/top/bottom/back/front_face_brightness()`), plus pitch /
  yaw orientation bits (not used until non-cube blocks).
- `chunk_edge_length == 32`. `libcellulose` already links `raylib` (only the demo
  uses it; the core mesher must not).

---

## Decisions (invented here — verify before executing)

| # | Question | Decision | Rationale |
|---|----------|----------|-----------|
| D1 | Output type | `MeshVertex{ Vec3 position; Vec3 normal; f32 u, v; f32 brightness; u32 block_id; }` and `ChunkMesh{ std::vector<MeshVertex> vertices; std::vector<u32> indices; }` in `inc/cellulose/mesh.hpp`. No raylib types. | Engine-neutral; the demo converts to `raylib::Mesh`. |
| D2 | Vertex space | Chunk-local `f32` in `[0, chunk_edge_length]`. The consumer offsets by `chunk_position * chunk_edge_length`. | Relocatable meshes; f32 is plenty for 0–32. |
| D3 | Winding / front face | CCW = front. `(axis, u, v)` with `u = (axis+1)%3`, `v = (axis+2)%3` is right-handed, so a `+axis` face emits corners `(0,0)(w,0)(w,h)(0,h)` as `{0,1,2, 0,2,3}`; a `-axis` face reverses. | Matches OpenGL / raylib default. |
| D4 | Which faces are emitted | A solid cell's face toward direction `d` is emitted iff the neighbour cell in `d` is **not** solid (per the predicate). Neighbour in an **absent chunk** counts as not solid ⇒ face emitted (correct as chunks stream in). | Standard hidden-face culling. |
| D5 | Merge key | Two adjacent faces merge only if identical `(block_id, face_brightness_d)`. Key = `(block_id << 8) | brightness`. | Brightness differences must stay visible; orientation bits deferred (cubes only). |
| D6 | Neighbour sampling | Copy a `(n+2)³` apron grid once per mesh: `solid` for all cells, `block_id` + the six brightnesses for the interior `n³`. Cells read via `Chunk::read_hot` snapshots (through `World`); an absent chunk fills `solid = false`. Then greedy-mesh the local grid. | One O(n³) copy, then cache-friendly meshing; no per-face world lookups. |
| D7 | Greedy algorithm | Per the 6 face directions: for each of `n` slices, build an `n×n` key mask (0 = no face), then repeatedly take an unclaimed cell, extend width while the key matches, extend height while the whole row matches, emit one quad, clear the rectangle. | Textbook 0fps greedy meshing. |
| D8 | LOD | `mesh_chunk_lod(world, chunk_position, level, is_solid)`. Block size `B = 1 << level` (`level` 0–5 ⇒ `B` 1–32); resolution `n = chunk_edge_length >> level`. A macro-cell is solid if **any** of its `B³` cells is solid; its `block_id` / brightness come from the **first solid** cell found (row-major). Greedy-mesh the `n³` grid, scaling positions and sizes by `B` so the mesh still spans `[0, 32]`. `level == 0` is exactly `mesh_chunk`. | "Downsample voxel clusters into macro-blocks" (README); any-solid keeps silhouettes; first-solid attribute is cheap and good enough at distance. |
| D9 | Shared core | Both entry points build a `std::vector<Sample>` apron grid (`Sample{ bool solid; u16 block_id; std::array<u8,6> brightness; }`) and call one `impl::greedy_mesh(samples, n, block_scale) -> ChunkMesh`. | One tested algorithm, two samplers. |
| D10 | Header layout | `inc/cellulose/mesh.hpp` holds the types, `impl::greedy_mesh`, and both `mesh_chunk*` entry points. Added to `cellulose.hpp`. The raylib bridge lives only in `src/main.cpp`. | Matches the repo; keeps raylib out of the core. |
| D11 | Namespacing | `MeshVertex` / `ChunkMesh` are value types in `namespace cellulose`; `mesh_chunk` / `mesh_chunk_lod` are free functions there; `greedy_mesh` and `Sample` in `namespace cellulose::impl`. | Consistent with `raycast.hpp` / `volume.hpp`. |

---

## Global Constraints

- Conventions per `ARCHITECTURE_SPEC.md` § "Conventions".
- Header-only core; `libcellulose` stays `INTERFACE`; raylib stays out of `inc/`.
- No API regression; existing tests unchanged.
- TDD per task: RED (a vertex/index/quad count assertion fails) → GREEN → commit.
- The demo change is verified by capturing stdout + one screenshot path check,
  not by an automated GPU test.

---

## File Structure

**Created:**
- `inc/cellulose/mesh.hpp` — `MeshVertex`, `ChunkMesh`, `impl::Sample`,
  `impl::greedy_mesh`, `mesh_chunk`, `mesh_chunk_lod`.
- `tests/test_mesh.cpp`.

**Modified:**
- `inc/cellulose/cellulose.hpp` — add `mesh.hpp`.
- `src/main.cpp` — build a small world, `mesh_chunk` it, upload to a raylib
  `Mesh`, orbit-camera render; keep the existing stdout lines.
- `ARCHITECTURE_SPEC.md` §4 — as-built; add D1–D11 to the decision table.
- `REMAINING_TASKS.md` — tick Phase 4; deferrals to the backlog.

---

## Task 1 — `mesh.hpp` types + `impl::greedy_mesh`

**Files:** create `inc/cellulose/mesh.hpp`, `tests/test_mesh.cpp`.

- [ ] `MeshVertex`, `ChunkMesh` (+ `empty()`), `impl::Sample`.
- [ ] `impl::greedy_mesh(const std::vector<Sample> &, i32 n, f32 block_scale) -> ChunkMesh`
      per D3 / D6 / D7. Apron indexing helper `at(x,y,z)` over `-1 .. n`.
- [ ] RED→GREEN against **hand-built sample grids** (no `World` yet):
      - all-empty grid ⇒ `empty()`.
      - one solid cell ⇒ 6 quads (24 verts, 36 indices); each axis normal once;
        positions within the cell's unit bounds.
      - 2×2×2 solid ⇒ 6 quads still (each face merged 2×2); a vertex at the
        far corner.
      - two cells with different brightness on one shared-direction face ⇒ that
        direction yields 2 quads, the others still merge.
      - a solid cell whose neighbour in `+X` is solid ⇒ no `+X` quad.
      - winding: for one `+X` quad, the two triangles' computed normal is `+X`.
- [ ] Commit: "Phase 4 T1: add `mesh.hpp` greedy mesher (grid-level)."

## Task 2 — `mesh_chunk` (World-backed)

**Files:** `inc/cellulose/mesh.hpp`, `tests/test_mesh.cpp` (append).

- [ ] `mesh_chunk(world, chunk_position, is_solid)`: build the `34³` `Sample`
      apron via `World` + `Chunk::read_hot` snapshots (absent chunk ⇒
      `solid = false`), then `greedy_mesh(samples, 32, 1.0f)`.
- [ ] RED→GREEN:
      - empty chunk ⇒ empty mesh.
      - one solid cell in a chunk ⇒ 24 verts / 36 indices, positions offset to
        the cell.
      - fully solid chunk ⇒ 6 quads (24 verts) spanning `[0,32]`.
      - solid at local `(31,0,0)` **and** the neighbouring chunk's `(0,0,0)`
        solid ⇒ the `+X` face is culled (5 faces, not 6).
      - solid at local `(31,0,0)` with the neighbour chunk **absent** ⇒ `+X`
        face present.
- [ ] Commit: "Phase 4 T2: add `mesh_chunk()` with cross-chunk face culling."

## Task 3 — `mesh_chunk_lod`

**Files:** `inc/cellulose/mesh.hpp`, `tests/test_mesh.cpp` (append).

- [ ] `mesh_chunk_lod(world, chunk_position, level, is_solid)` per D8: downsample
      to `n = 32 >> level`, any-solid macro-cells, first-solid attributes,
      `greedy_mesh(samples, n, static_cast<f32>(1 << level))`.
- [ ] RED→GREEN:
      - `level == 0` on a scene equals `mesh_chunk` (same vertex count).
      - fully solid chunk at `level 5` ⇒ 6 quads spanning `[0,32]` (24 verts).
      - one solid cell at `level 1` ⇒ 6 quads spanning `[0,2]`.
      - a checkerboard that greedy-merges to N quads at `level 0` merges to
        fewer at `level 1`.
- [ ] Commit: "Phase 4 T3: add `mesh_chunk_lod()` macro-block meshing."

## Task 4 — raylib render demo

**Files:** `src/main.cpp`.

- [ ] After the existing stdout demo: fill a small region of `world` with a
      couple of block ids + varied face brightness; `mesh_chunk` chunk `(0,0,0)`;
      convert `ChunkMesh` → `raylib::Mesh` (`UploadMesh`); `LoadModelFromMesh`;
      draw it each frame under the existing orbit camera with `DrawModel`; keep
      `DrawGrid` / FPS. Print `vertices: N  triangles: M`.
- [ ] Unload the model / mesh before `CloseWindow`.
- [ ] Verify: build; run detached, capture stdout (`vertices:` / `triangles:`
      lines present and non-zero), screenshot to a scratch path, kill.
- [ ] Commit: "Phase 4 T4: render a greedy-meshed chunk in the demo."

## Task 5 — Umbrella + docs + sweep

- [ ] `cellulose.hpp` — add `mesh.hpp`.
- [ ] `ARCHITECTURE_SPEC.md` §4 → as-built (mesh types, the two entry points, the
      apron-sampling + merge-key approach, LOD rule); D1–D11 into the table.
- [ ] `REMAINING_TASKS.md` — Phase 4 checked off; AO / atlas UV / non-cube /
      transparency / mesh caching / LOD seams to the backlog.
- [ ] Full run: `ctest` green; `clang-format --dry-run --Werror` clean on touched
      files; ASan build links clean; demo prints the vertex / triangle counts.
- [ ] Commit: "Phase 4 T5: umbrella + docs; close out Phase 4."

---

## Verification (end-to-end)

1. Clean build, warning-free for `inc/cellulose/*`; `cellulose` + `cellulose_tests` link.
2. `ctest` — all Phase 1–4 cases pass; earlier phases unchanged.
3. Greedy invariant: for a random solid pattern in one chunk, every emitted quad's
   4 corners are coplanar, its area equals `w*h`, and the union of all quad areas
   for a given face direction equals the number of visible faces in that direction
   (no overlap, no gap).
4. Cull invariant: a fully solid 3×3×3 block emits exactly 6 quads (its shell).
5. Demo: window opens showing a meshed blocky shape; stdout has
   `vertices: <n>` / `triangles: <m>` with `m == n / 2` (quads → 4 verts, 6 idx…
   actually `m == indices/3`, `verts == quads*4`), both non-zero.
6. `clang-format` clean; ASan build links.

---

## Deferred (backlog)

- **Ambient occlusion** — per-vertex AO from the 3 neighbours at each corner;
  breaks quad merging unless AO is part of the merge key.
- **Texture-atlas UVs** — mesher emits tile-space `(u,v)` in `[0,w]×[0,h]`; a
  `block_id → atlas rect` step (needs `Block` to carry texture info).
- **Non-cube blocks** — consume `HotCellAttribute` pitch / yaw to emit rotated
  or partial geometry (stairs, slabs, fences).
- **Transparency / cutout** — a second pass, back-to-front, no face culling
  between different transparent ids.
- **Incremental remesh** — dirty-flag chunks on edit, remesh only those; a mesh
  cache keyed by `ChunkPosition`.
- **LOD seam stitching** — skirts or transition cells between adjacent chunks
  meshed at different levels.
- **Threaded meshing** — run `mesh_chunk` on a worker pool; it already only
  needs read access (seqlock snapshots).
