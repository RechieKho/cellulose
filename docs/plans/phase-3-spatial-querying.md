# Phase 3 — Spatial Querying — Implementation Plan

**Goal:** Give `cellulose` the three spatial queries the README promises, built on
the `World` / `Chunk` types: **raycasting** (voxel traversal), **volumetric
queries** (cells within an AABB / sphere), and **collision detection**
(move an AABB against solid voxels, report the correction + normal).

**Spec:** `ARCHITECTURE_SPEC.md` §3 (design intent from `README.md`
§ "Spatial Querying"). This plan fills in every concrete decision and records it
under *Decisions* — correct any before/while executing.

**Scope in:** a minimal `Vector3<T>` math type; `raycast()` (Amanatides & Woo
DDA); `for_each_cell_in_aabb` / `for_each_cell_in_sphere`; `move_aabb()`
axis-separated swept collision. All read the world through the per-chunk seqlock
so a query is safe against concurrent hot-tier writers.

**Scope out (deferred — see end):** raycast chunk-pointer caching; bulk
whole-chunk snapshots for volume queries; continuous collision for very fast
movers; sphere-casts / capsule casts; a registry-driven default solidity
predicate (needs `Block` to gain a solidity flag).

---

## Context

- After Phase 2: `World` hands out stable `Chunk*` (under a directory
  `shared_mutex`); `Chunk` exposes `read_hot(fn)` returning a by-value snapshot
  under its seqlock. `coordinate.hpp` has `WorldPosition` (`i64` per axis),
  `to_chunk_position` / `to_local_position`. `morton.hpp` has
  `encode_cell_index`. `chunk_edge_length == 32`.
- There is **no float vector type** and **no notion of "solid"** — `Block` only
  carries a `name`. Queries therefore take a caller-supplied predicate
  `bool(const HotCellAttribute &)` and this plan adds `Vector3<T>`.
- Math: `<cmath>` (`std::floor`, `std::sqrt`), `<optional>`, `<limits>` are fine.

---

## Decisions (invented here — verify before executing)

| # | Question | Decision | Rationale |
|---|----------|----------|-----------|
| D1 | Math type | `template <typename T> struct Vector3 { T x, y, z; }` in `inc/cellulose/vector.hpp`, directly in `namespace cellulose` (value type, like the coordinate structs). Aliases `Vec3 = Vector3<f32>`, `Vec3d = Vector3<f64>`, `Vec3i = Vector3<i32>`. Defaulted `operator==`; member `+ - * /` (vector±vector, vector·scalar); free `dot`, `cross`, `length`, `length_squared`, `normalized`, `component_min`, `component_max`. | Small, self-contained, no raylib dependency (keeps C2 open). |
| D2 | Float point → cell | `auto to_cell(const Vec3d &) -> WorldPosition` (component-wise `floor`) in `vector.hpp`; `auto to_point(const WorldPosition &) -> Vec3d` for the inverse (cell min corner). | One obvious mapping; floor handles negatives. |
| D3 | Solidity | Every query takes `Predicate&& p_is_solid` = `bool(const HotCellAttribute &)`. No default. A cell in an **absent chunk** is treated as **not solid** (empty space) by raycast and collision, and **skipped** by volume queries. | `Block` has no solidity concept yet; predicate defers it without blocking Phase 3. |
| D4 | Cell resolution under concurrency | Queries look up the chunk via `world.find_chunk` (directory shared lock), then read the cell as `chunk->read_hot([&](const auto &hot){ return hot[index]; })` — a by-value `HotCellAttribute` snapshot under the seqlock. The predicate/visitor runs **outside** the seqlock closure on that snapshot. | Safe against concurrent hot writers; the visitor never runs twice on a retry. |
| D5 | Raycast algorithm | Amanatides & Woo 3-D DDA. `Ray{ Vec3d origin; Vec3d direction; }` (direction normalised inside `raycast`; zero-length ⇒ no hit). `raycast(World<> &, const Ray &, f64 p_max_distance, Predicate) -> std::optional<RaycastHit>`. `RaycastHit{ WorldPosition cell; Vec3i normal; f64 distance; }`. If the ray starts **inside** a solid cell, that cell is the hit with `distance == 0` and `normal == {0,0,0}`. | Textbook, integer-stepped, exact. |
| D6 | Raycast world lookup | v1 calls `find_chunk` + `read_hot` for **every** stepped cell. | Correct and simple; the per-step chunk lookup is the documented optimisation target (deferred). |
| D7 | Volume queries | `Aabb{ Vec3d min; Vec3d max; }` in `vector.hpp`. `for_each_cell_in_aabb(World<> &, const Aabb &, Visitor)` and `for_each_cell_in_sphere(World<> &, const Vec3d &center, f64 radius, Visitor)`; `Visitor` = `void(const WorldPosition &, const HotCellAttribute &)` invoked for **every existing cell** whose unit voxel the region touches. Iteration is chunk-major: compute the `ChunkPosition` range, skip via `find_chunk`, walk local cells. Sphere test: voxel included if its **closest point** to the centre is within `radius`. | Chunk-major skips empty space; closest-point avoids corner-clipping artefacts. |
| D8 | Collision | `move_aabb(World<> &, const Aabb &p_box, const Vec3d &p_velocity, Predicate) -> CollisionMove`. `CollisionMove{ Vec3d position; Vec3i normal; bool collided; }` — `position` is `p_box.min` after the (possibly shortened) move; `normal` is the last face hit (`{0,0,0}` if none). Resolution is **axis-separated**: apply X, scan the voxels the swept box covers, clamp to the first blocker, zero that axis; repeat Y, then Z. This gives "collide and slide" and clean axis normals. | Standard, tunnel-free for `|v| < 1 cell/step`, and enough as a "baseline for custom physics" (README). Continuous collision for fast movers is deferred. |
| D9 | Header layout | `vector.hpp`, `raycast.hpp`, `volume.hpp`, `collision.hpp` — flat, added to `cellulose.hpp`. `raycast`/`volume`/`collision` include `vector.hpp` + `world.hpp`. | Matches the repo. |
| D10 | Namespacing | Free functions directly in `namespace cellulose` (like `encode_cell_index`, `to_chunk_position`). No `impl::` template dance — these are functions, not stateful classes. `Vector3` / `Ray` / `Aabb` / hit structs are plain value types in `namespace cellulose`. | Consistent with `morton.hpp` / `coordinate.hpp`. |

---

## Global Constraints

- Conventions per `ARCHITECTURE_SPEC.md` § "Conventions" (`p_` params, trailing
  returns, tabs, `CEL_*_HPP` guards, `{ a, b }` brace spacing, attribution
  trailers, one commit per task).
- Header-only; `libcellulose` stays `INTERFACE`.
- No API regression; all existing tests keep passing.
- TDD per task: RED (analytic assertion fails) → GREEN → commit. Query tests use
  hand-built worlds with known solids and closed-form expected hits.
- Queries must build clean under `-DCELLULOSE_SANITIZER=address`. (Running the
  instrumented tests needs the MSVC ASan runtime DLL on `PATH`; that setup is a
  CI concern — locally the portable fuzz / stress tests are the signal.)

---

## File Structure

**Created:**
- `inc/cellulose/vector.hpp` — `Vector3<T>`, `Vec3` / `Vec3d` / `Vec3i`, `Aabb`,
  ops, `to_cell` / `to_point`.
- `inc/cellulose/raycast.hpp` — `Ray`, `RaycastHit`, `raycast()`.
- `inc/cellulose/volume.hpp` — `for_each_cell_in_aabb`, `for_each_cell_in_sphere`.
- `inc/cellulose/collision.hpp` — `CollisionMove`, `move_aabb()`.
- `tests/test_vector.cpp`, `tests/test_raycast.cpp`, `tests/test_volume.cpp`,
  `tests/test_collision.cpp`.

**Modified:**
- `inc/cellulose/cellulose.hpp` — add the four headers.
- `ARCHITECTURE_SPEC.md` §3 — as-built; add D1–D10 to the decision table.
- `REMAINING_TASKS.md` — tick Phase 3; move deferrals to the backlog.

---

## Task 1 — `vector.hpp`

**Files:** create `inc/cellulose/vector.hpp`, `tests/test_vector.cpp`.

- [ ] `Vector3<T>` with defaulted `operator==`, member `operator+ - *(scalar)
      /(scalar)` and unary `-`; free `dot`, `cross`, `length_squared`, `length`,
      `normalized` (zero vector ⇒ zero), `component_min`, `component_max`.
- [ ] `Aabb{ Vec3d min; Vec3d max; }`; helpers `center()`, `contains(Vec3d)`,
      `intersects(Aabb)`.
- [ ] `to_cell(const Vec3d &) -> WorldPosition` (floor); `to_point(const WorldPosition &) -> Vec3d`.
- [ ] RED→GREEN: arithmetic identities; `to_cell({-0.1, 0.0, 2.9}) == {-1, 0, 2}`;
      `normalized` of `{3,4,0}` has length 1; `Aabb::intersects` true/false cases.
- [ ] Commit: "Phase 3 T1: add `vector.hpp` (Vector3, Aabb, cell/point conversions)."

## Task 2 — `raycast.hpp` (Amanatides & Woo DDA)

**Files:** create `inc/cellulose/raycast.hpp`, `tests/test_raycast.cpp`.

- [ ] `Ray`, `RaycastHit` per D5. `raycast(world, ray, max_distance, is_solid)`:
      normalise direction; `cell = to_cell(origin)`; per axis `step = sign(dir)`,
      `tMax = ` distance to first voxel boundary, `tDelta = |1 / dir|` (∞ when
      `dir == 0`); loop: if the current cell is solid (via D4 lookup) return the
      hit; advance along the smallest `tMax`, set `normal = -step` on that axis,
      bump `tMax += tDelta`; stop when the travelled distance exceeds
      `max_distance`.
- [ ] Origin-inside-solid ⇒ `{cell, {0,0,0}, 0.0}`.
- [ ] RED→GREEN tests (single chunk + across a chunk boundary):
      - axis-aligned ray down +X hits the first solid at the expected cell,
        `normal == {-1,0,0}`, `distance` exact.
      - diagonal ray hits the analytically-correct cell/face.
      - ray into empty space / past `max_distance` ⇒ `std::nullopt`.
      - ray crossing from chunk (0,0,0) into (1,0,0) hits a solid at x=32.
      - negative-direction ray (`step == -1`) hits the expected face.
- [ ] Commit: "Phase 3 T2: add `raycast()` (Amanatides & Woo voxel DDA)."

## Task 3 — `volume.hpp`

**Files:** create `inc/cellulose/volume.hpp`, `tests/test_volume.cpp`.

- [ ] `for_each_cell_in_aabb(world, aabb, visitor)`: cell range
      `[to_cell(aabb.min), to_cell(aabb.max)]`; iterate chunk-major, `find_chunk`,
      skip when absent; for each existing local cell in range, snapshot via D4 and
      call `visitor(world_position, snapshot)`.
- [ ] `for_each_cell_in_sphere(world, center, radius, visitor)`: iterate the
      bounding AABB; include a cell when the closest point of its unit voxel to
      `center` is within `radius`.
- [ ] RED→GREEN:
      - AABB spanning a 2×2×2 block of solids in one chunk visits exactly 8 cells
        with the right positions.
      - AABB straddling a chunk boundary visits cells from both chunks; an absent
        neighbour contributes nothing.
      - sphere of radius 1.5 at a cell centre visits the centre + 6 face
        neighbours (not the 12 edge / 8 corner cells).
- [ ] Commit: "Phase 3 T3: add AABB / sphere volume queries."

## Task 4 — `collision.hpp`

**Files:** create `inc/cellulose/collision.hpp`, `tests/test_collision.cpp`.

- [ ] `move_aabb(world, box, velocity, is_solid) -> CollisionMove` per D8:
      axis-separated. For each axis in X,Y,Z: tentatively move `box` by that
      component; over the voxel span the swept box now covers on the leading
      face, find the nearest solid; if any, clamp the box flush against it,
      set `normal` on that axis (sign = `-sign(velocity_axis)`), zero the axis.
      `position = box.min` after all three; `collided = normal != {0,0,0}`.
- [ ] RED→GREEN:
      - box moving +X into a wall stops flush (`min.x == wall_x - box_width`),
        `normal == {-1,0,0}`, `collided`.
      - box moving diagonally into an inside corner slides: stops on X, still
        moves on Y (or the reverse), `position` correct.
      - box moving through open space reaches `box.min + velocity`, `!collided`.
      - falling box (`velocity == {0,-v,0}`) lands on a floor at the expected
        height.
- [ ] Commit: "Phase 3 T4: add `move_aabb()` swept AABB-vs-voxel collision."

## Task 5 — Umbrella + docs + sweep

- [ ] `cellulose.hpp` — add `vector.hpp`, `raycast.hpp`, `volume.hpp`, `collision.hpp`.
- [ ] `ARCHITECTURE_SPEC.md` §3 → as-built: the `Vector3` type, the three query
      families, the predicate contract, the D4 concurrency approach; D1–D10 into
      the decision table; note the C2 status (still raylib-free).
- [ ] `REMAINING_TASKS.md` — Phase 3 checked off; deferrals to the backlog.
- [ ] Full run: `ctest` green; `-DCELLULOSE_SANITIZER=address` green;
      `clang-format --dry-run --Werror` clean on all touched files; demo unchanged.
- [ ] Commit: "Phase 3 T5: umbrella + docs; close out Phase 3."

---

## Verification (end-to-end)

1. Clean build, warning-free for `inc/cellulose/*`; `cellulose` + `cellulose_tests` link.
2. `ctest` — all Phase 1–3 cases pass; Phase 1–2 cases unchanged.
3. `-DCELLULOSE_SANITIZER=address` configures + builds + links clean (running it
   needs the ASan runtime on `PATH` — a CI job).
4. Raycast cross-check: for a handful of random rays into a known scene, the hit
   matches a brute-force "step 0.01 along the ray until solid" reference.
5. Collision cross-check: `move_aabb` with tiny sub-cell velocities integrated
   over many steps lands in the same resting position as one large step.
6. `clang-format` clean.

---

## Deferred (backlog)

- **Raycast chunk caching** — hold the current `Chunk*` and only re-resolve on a
  chunk-boundary crossing (D6). Same idea for `for_each_cell_in_*`.
- **Bulk chunk snapshot** for volume queries — one `read_hot` copying the local
  sub-range instead of a seqlock acquisition per cell.
- **Continuous / conservative-advancement collision** for `|velocity| > 1` cell.
- **Sphere-cast, capsule-cast, ray-vs-AABB-of-entities.**
- **Registry-driven solidity** — add `bool solid` / `bool opaque` to `Block`, so
  queries can take a `BlockRegistry` instead of a hand-written predicate.
- **`BlockRegistryBuilder::build()` bug** (pre-existing) — it pre-sizes `Store`
  to N then `push_back`s N more, and indexes `name_id_map` 0..N-1 while the built
  blocks land at N..2N-1. Unrelated to Phase 3 but blocks a real registry-backed
  predicate.
