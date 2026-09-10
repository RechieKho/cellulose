# STATE — working notes for the next agent

Practical handoff notes: what's built, how to build/test it, and the gotchas that
will otherwise cost you an hour. For the *design*, read `ARCHITECTURE_SPEC.md`;
for the *backlog*, `REMAINING_TASKS.md`; for phase-by-phase rationale,
`docs/plans/`.

---

## Where things are

| File | What |
|------|------|
| `README.md` | product pitch + the 4-subsystem design narrative + short usage examples |
| `ARCHITECTURE_SPEC.md` | **as-built** reference — every header, every locked decision, the decision table |
| `REMAINING_TASKS.md` | live backlog; all 4 phases + every "adopt" verdict are done, the rest is trigger-gated |
| `docs/plans/phase-{2,3,4}-*.md` | per-phase implementation plans (executed) |
| `docs/plans/design-followups.md` | the design-review decisions + triggers for the deferred ones |
| `docs/plans/concurrency-benchmarks.md` | the benchmark plan (executed) |
| `docs/plans/texture-management.md` | the texture subsystem plan + decisions (executed) |
| `docs/benchmarks/RESULTS-2026-09-10-i7-14700HX.md` | benchmark numbers + verdicts, with a post-D3/D5 re-run section |
| `inc/cellulose/*.hpp` | the library (header-only) |
| `src/main.cpp` | the minimal voxel game demo (the only `.cpp` in `src/`) |
| `tests/test_*.cpp` | doctest cases, auto-globbed |
| `benchmarks/` | dependency-free concurrency benchmark harness (opt-in build) |

There is no `CLAUDE.md`; this file is the closest thing. `.superpowers/` and the
superpowers plugin were removed / disabled — don't look for them.

---

## Current state

- **All four README subsystems are implemented and tested**: core data structure
  (Phase 1), chunk-level concurrency (Phase 2), spatial querying (Phase 3),
  greedy meshing + LOD (Phase 4).
- **Design follow-ups done**: extensible hot type + `HotAttribute` concept,
  `Chunk::revision()`, mesher rule split (`has_geometry` / `is_hidden`), opt-in
  `shared_ptr` chunk storage (`ChunkStorage::Shared`), opt-in ambient occlusion,
  texture management, T-junction welding.
- **Benchmark verdicts implemented**: strict `atomic_ref` hot tier is the
  **default** (D3), the `World` directory is **sharded** (D5), `Chunk::snapshot_hot`
  is the mesher's bulk read.
- **92 tests, all green.** `main` == `origin/main`. Builds CI green on
  Linux/macOS/Windows; the `Benchmarks` workflow's TSan + ASan run is clean.
- Headers (`inc/cellulose/`): `types coordinate morton cell block texture
  atlas_builder inspect sync seqlock rwlock chunk world cursor vector raycast
  volume collision mesh` + `cellulose.hpp` (umbrella, all of the above) +
  `raylib.hpp` (opt-in, **not** in the umbrella).

---

## Build & run

Default toolchain here is **MSVC `cl.exe` via the Visual Studio generator** (not
clang — despite what old notes say). C++20, multi-config generator.

```sh
cmake -S . -B build          # ~2-3 min the first time (raylib builds from FetchContent)
cmake --build build          # incremental builds are seconds; add --config Release for Release
ctest --test-dir build       # <4 s; 92 cases
```

- Test binary: `build/tests/<Config>/cellulose_tests.exe`. Demo:
  `build/<Config>/cellulose.exe`.
- The demo (`src/main.cpp`) is a **minimal voxel game** — fly camera, hold-LMB to
  mine (a custom cold-tier `Damage` attribute counts hits), RMB place, per-face
  **textures** from a procedural atlas + the atlas-tiling shader, baked **ambient
  occlusion** and **T-junction welding** (`MeshOptions`), `FLAG_VSYNC_HINT |
  FLAG_MSAA_4X_HINT`. Gated behind `CELLULOSE_BUILD_DEMO` (ON by default).
- **The demo opens a raylib window and blocks.** Smoke-check in a script: run
  detached, `sleep 5`, then `taskkill //F //IM cellulose.exe`. Expect `world: 9
  chunks generated` on stdout, custom shaders compiled, 9 meshes uploaded, no GL
  errors.
  **Stray `cellulose.exe` from earlier runs holds `build/` locked** (`rm -rf
  build` → "Device or resource busy") — `taskkill //F //IM cellulose.exe` first.
- CMake options: `CELLULOSE_BUILD_TESTS` (ON top-level), `CELLULOSE_BUILD_DEMO`
  (ON), `CELLULOSE_BUILD_BENCHMARKS` (OFF), `CELLULOSE_SANITIZER=<address|thread|
  undefined>`, `CELLULOSE_LOOSE_ATOMICS` (OFF — see gotcha 5),
  `CELLULOSE_SEQLOCK_STATS` (OFF — benchmark instrument). Instrumentation flags
  are `option()`s plumbed via `target_compile_definitions` because a raw `-D...`
  gets mangled by the MSYS shell (`/D...` → `D:/Programs/Git/D...`).

### CI

`.github/workflows/`:
- `build_{linux,macos,windows}.yml` — **build only, no ctest**, Release + a
  debug-ish config, all three platforms. GCC/Clang on Linux/macOS are **LP64**
  and stricter than MSVC (see below).
- `lint.yml` — `clang-format` on **`src/**` only**. A broken test or an `inc/` /
  `tests/` format slip **passes CI** — check locally.
- `benchmarks.yml` — **`workflow_dispatch` only** (`gh workflow run Benchmarks`).
  Release timing run + TSan + ASan on the mixed workload with
  `halt_on_error=1`. The value is the sanitizer run, not the (noisy) numbers.
  Run it after touching `chunk.hpp` / `seqlock.hpp` / `world.hpp` / `mesh.hpp`.

### Sanitizers

- **ThreadSanitizer: not on Windows at all.** Linux/macOS + GCC/Clang only — use
  the `Benchmarks` workflow.
- **ASan on MSVC** builds and links but the exe won't start without
  `clang_rt.asan_*.dll` on `PATH` (exits `0xC0000135`, ctest hangs). Treat ASan
  as a CI/Linux concern.
- The portable local signal is the threaded **stress tests** — they fail
  deterministically if you remove the lock they exercise.

### GCC / Linux vs MSVC — things that bit and will bite again

- **`<shared_mutex>` does not pull `<mutex>`** on libstdc++. A header using
  `std::unique_lock` / `std::lock_guard` must `#include <mutex>` itself.
- **`uint_fast16_t == uint_fast32_t == uint_fast64_t == unsigned long` on LP64
  Linux** — they collapse, which made `libmorton`'s templated LUT overloads
  ambiguous. `libmorton` was **dropped**; `morton.hpp` is a self-contained
  magic-bits interleave. Don't reintroduce a `uint_fast*`-heavy templated dep
  without a Linux check.
- **`std::hardware_destructive_interference_size`** — GCC warns (`-Winterference-size`)
  when it's used across TUs, and Apple ARM reports 128. Pinned to
  `cache_line_size = 64` in `sync.hpp`; don't switch back to the std facility.
- MSVC is lenient about missing transitive includes and generous overload
  resolution — a clean local build is not proof CI is clean. New `std::` uses
  need their own `#include`.

---

## Gotchas (the ones that will bite)

1. **`impl::` name lookup.** Inside `namespace cellulose::impl`, the bare names
   `SeqLock` / `RWLock` / `Padded` / `HotCellAttribute` bind to the *unspecialised
   class templates* (`impl::SeqLock` …), **not** the `cellulose::` aliases. Write
   `cellulose::SeqLock`, `cellulose::HotCellAttribute`, … in `impl` code. Has
   bitten every phase. Symptom: `C2955 / C7602: use of class template requires
   template argument list`.

2. **doctest test names must not contain `;`.** `doctest_discover_tests` passes
   names through CMake, which splits on `;` → the case silently registers as two
   ctest entries. Use "and" / "—".

3. **`Chunk` is non-movable and non-copyable** (embeds `std::mutex` /
   `std::shared_mutex` / `std::atomic`). `World` stores `unique_ptr<Chunk>`
   (default) or `shared_ptr<Chunk>` (`ChunkStorage::Shared`). No container of
   `Chunk` by value compiles.

4. **`clang-format`.** `.clang-format`: `IncludeBlocks: Preserve` + `SortIncludes`.
   Put the header-under-test in its **own paragraph** right after
   `<doctest/doctest.h>`, blank line, then the rest — otherwise `<algorithm>`
   sorts above `<cellulose/foo.hpp>`. Run `clang-format -i` on every touched
   file (`inc/`, `tests/`, `src/`) before committing — only `src/` is CI-gated.
   Style: **tabs**, `ColumnLimit: 0`, `{ a, b }` brace spacing, `} //namespace x`,
   trailing return types, `p_`-prefixed params, no `m_` on public members.
   `.clang-format` says `Standard: c++17` but the code is C++20 (concepts,
   `std::atomic_ref`, `std::default_initializable`, designated initializers).

5. **Hot tier is `std::atomic_ref` by default** (D3 adopted). `chunk.hpp`
   auto-defines `CELLULOSE_STRICT_ATOMICS` unless `CELLULOSE_LOOSE_ATOMICS` is
   set. Consequences:
   - a **`write_hot` closure must assign whole elements** (`hot[i] =
     HotCellAttribute{...}`), never fields — a compile error under strict.
   - `read_hot`'s `hot` arg is an `impl::AtomicReadView` — `hot[i]` yields a
     **value**, not a reference.
   - `Chunk::snapshot_hot` under strict is N `atomic_ref` loads, not a `memcpy` —
     measurably slower than the loose path when a writer is concurrently active
     (B7: edit-heavy meshing ~10-20% slower). This is the accepted D3 trade for
     the TSan-clean layer.
   - `-DCELLULOSE_LOOSE_ATOMICS=ON` restores the plain-array benign race (faster
     writes, TSan-flagged, UB). The torn-read stress tests (`test_chunk.cpp`,
     `bench_seqlock`) only compile in that config (`#ifdef CELLULOSE_LOOSE_ATOMICS`).

6. **Custom hot attribute types** must satisfy `concept HotAttribute`:
   `std::is_trivially_copyable_v` + `std::default_initializable` + a `block_id`
   member convertible to `BlockID`. That's all — brightness / orientation /
   texture are read through the `face_brightness(attr, face)` and
   `face_texture(attr, face)` customization points, which flat-shade / fall back
   to `block_id` when a custom type provides no overload.

7. **The `mesh_chunk` / `mesh_chunk_lod` overload set is concept-disambiguated.**
   Every form also takes a **trailing `const MeshOptions & = {}`**. The 4-/5-arg
   forms are told apart by positive concepts, not arity:
   - `(world, cp, is_solid)` — opaque cubes.
   - `(world, cp, is_solid, texture_of)` — `requires FaceTextureResolver<TextureOf>`
     (the callable returns exactly `TextureID` from `(attr, i32)` — **not** `auto`
     or a bare `u32`, or overload resolution drops it).
   - `(world, cp, has_geometry, is_hidden)` — `requires FaceHiddenRule<IsHidden>`
     (returns `bool`-ish from `(attr, attr)`).
   - `(world, cp, has_geometry, is_hidden, texture_of)` — both concepts.
   - `+ MeshOptions` on any of the above.
   Don't loosen those `requires` clauses — `MeshOptions` in the 4th/5th slot
   would then be mistaken for an `is_hidden` rule or a resolver → ambiguous.

8. **`impl::face_layout[6]`** (`mesh.hpp`) is the single source of truth for
   per-face merge/UV axes, texture orientation and triangle winding. Side faces:
   `v = 0` at world `+Y`, `u` left-to-right viewed from outside, so a column
   texture (grass side, log) reads upright. `emit_quad` takes a `FaceLayout`;
   `sample_chunk`'s AO corner sampling reads the same table. Change the
   convention *there*, never with scattered `(axis+1)%3` arithmetic (that was the
   original bug — only Z faces got `v = world-Y`).

9. **Greedy merge key = `(resolved-texture-id, brightness [, uniform AO level])`**
   — **not** `block_id`. `block_id` rides a parallel `block_at` array, copied from
   the merged run's origin cell. `texture_id == 0` = "unset" → falls back to
   `block_id`, so real texture ids start at 1 (`atlas_builder::pack_grid` leaves
   cell 0 unused). AO on: a face with **uniform** corner AO merges within that
   level; any per-corner variation is emitted as a 1×1 quad and never merges.

10. **`MeshOptions` features are opt-in and geometry-preserving when off.**
    - `ambient_occlusion`: 0fps corner AO from the 8 in-plane neighbours in the
      `+normal` layer; occluder test is `has_geometry`; **level-0 meshes only**
      (`mesh_chunk_lod` ignores it at `level > 0`). `MeshVertex::occlusion` is
      `1.0` when off.
    - `weld_t_junctions`: a post-pass over `greedy_mesh`'s output (each quad is
      exactly **4 vertices + 6 indices** — nothing else appends vertices, so
      `vertices[4k..4k+3]` is quad `k`). Splits every quad edge carrying another
      quad's vertex; re-fans split quads **from the polygon centroid** (a corner
      fan slivers a split edge); unsplit quads keep their original triangulation
      (AO flip included); compacts to referenced vertices. The demo also enables
      MSAA for the residual sub-pixel aliasing.

11. **`PackedCellAttributeCollection<size CellCount, typename... Attributes>`** —
    `CellCount` can't be defaulted (precedes the pack). Use the
    `PackedChunkAttributes<Attributes...>` alias (chunk-sized) when plugging a
    cold attribute into `Chunk`. `SparseChunkAttributes<...>` for the freezing
    tier. Packed elements must be ≤ 8 bytes, sparse > 8 (`static_assert`ed).

12. **`coordinate.hpp` must not include `chunk.hpp`.** It exposes
    `chunk_edge_length_shift` / `chunk_edge_length_mask` (literals `5` / `31`);
    `chunk.hpp` `static_assert`s they match `chunk_edge_length`. Dependency stays
    one-way.

13. **`to_chunk_position` narrows to `i32`** — exact only while `|world_axis| <
    2^(31 + shift)` (≈ ±2^36 blocks). Fine for any real world.

14. **`World` directory is sharded** — `shard_count = 16` (`world.hpp`), routed
    by `ChunkPositionHash & 15`, each shard a `Padded<{ shared_mutex, sub-map }>`.
    `has_chunk` / `find_chunk` / `chunk` / `remove_chunk` touch one shard.
    `for_each_chunk` and `chunk_count` lock **all** shards (shared, in index
    order) — so a `for_each_chunk` visitor that calls back into `world.chunk()`
    **deadlocks** (same hazard as the old single mutex). D5 re-run: lookups now
    scale ~12M→48M over 1-16 threads.

15. **`raylib.hpp` is not in the umbrella** and includes `<raylib.h>`. Only
    `src/main.cpp` uses it. `libcellulose` (the INTERFACE lib) does **not** link
    raylib — only the `cellulose` executable does.

16. **The atlas bridge assumes a uniform-grid atlas.** `to_raylib_mesh(mesh,
    atlas)` bakes `atlas.rect_of(texture_id).min` into `texcoords2`; the shipped
    shader does `origin + fract(tileUV) * uTileSize` with `uTileSize` a single
    uniform from `atlas.rect_of(0)`'s extent. Non-uniform packed sheets
    (`atlas_builder::pack`) need a custom shader with per-vertex tile size. Use
    `NEAREST` filter + `CLAMP` wrap. `GL_TEXTURE_2D_ARRAY` was rejected — raylib
    has no API for it and it forced per-platform raw GL.

17. **`atlas_builder::pack_grid(ids, tile_px)`** lays uniform tiles into a
    **near-square power-of-two** grid (`columns` = smallest pow2 with `c² ≥
    max(id)+1`), id `n` at cell `(n % columns, n / columns)`. `pack(span<TileSize>)`
    is the shelf packer for mixed sizes → explicit rects. `TextureAtlas` has
    `columns()` / `rows()` / `count()` / `rect_of(id)` and three modes
    (`grid` / `layers` / explicit). (A `1×N strip` was the original design —
    replaced; GPUs sample a square sheet better and a strip hits `GL_MAX_TEXTURE_SIZE`.)

18. **Line endings.** `core.autocrlf = true` here → git warns "LF will be
    replaced by CRLF" on every text-file commit. Harmless; the repo stores LF.

---

## The mesher, in one place (`inc/cellulose/mesh.hpp`)

It has accreted; this is the map.

- **`MeshVertex`** = `{ Vec3 position; Vec3 normal; f32 u, v; f32 brightness;
  u32 block_id; TextureID texture_id; f32 occlusion; }`. `operator==` covers all
  fields — a full-vertex compare in a test breaks when a field is added (none do
  today). Positions are chunk-local `[0, chunk_edge_length]`, integer-valued.
- **`MeshOptions`** = `{ bool ambient_occlusion; bool weld_t_junctions; }` —
  gotcha 7, 10.
- **`greedy_mesh(samples, size, block_scale, ao=false, weld=false)`** — public,
  tests call it directly. 0fps greedy meshing over a `MeshSample` grid; emits via
  `impl::emit_quad`; calls `impl::weld_t_junctions` at the end when asked.
- **`impl::sample_chunk<World, HasGeometry, IsHidden, TextureOfPtr>`** — builds
  the `n³ + apron` grid: one `Chunk::snapshot_hot` of the centre chunk (the bulk
  of the reads), `impl::ChunkCursor` per-cell for the 1-cell apron. Fills
  `visible` / `brightness` / `texture` / `face_occlusion` per face.
- **`impl::mesh_chunk_impl`** — the shared body all public overloads funnel into;
  threads `p_options.ambient_occlusion` / `.weld_t_junctions` through.
- **`face_texture` / `face_brightness` CPOs** (`cell.hpp`) — customization points,
  default to `block_id` / flat.
- **`BlockRegistry` is itself a `texture_of` resolver** — pass the registry
  directly as the 4th arg to `mesh_chunk`.
- **`impl::FaceTextureResolver` / `impl::FaceHiddenRule`** — the disambiguating
  concepts (gotcha 7).

---

## TODOs / backlog

`REMAINING_TASKS.md` is authoritative. Everything with an "adopt" / "yes" verdict
is **done**. What's left is trigger-gated:

**Deferred by decision — do NOT start without the trigger** (`docs/plans/design-followups.md`):
- **D4** CAS single-writer seqlock (drop the per-tier `std::mutex`) — trigger: a
  world-gen profile showing writer-vs-writer contention. (B1/B7: ~30% at 4
  writers, mild.) The ~160 B/chunk saving is the stronger motive.
- **Generational chunk handle** (replace `shared_ptr` under `ChunkStorage::Shared`)
  — trigger: streaming engines adopting `Shared` widely (B5: ~25-30% lookup cost).
- **D10** `sweep_aabb` (continuous collision) + sphere/capsule casts — trigger: a
  real use for fast movers / shape casts.

**Optional modules — self-contained, build on demand:**
- `ChunkMeshCache` — dirty set keyed by `ChunkPosition` on `Chunk::revision()`,
  remesh dirty chunks + their 6 face neighbours.
- **LOD seam stitching** — skirts between adjacent *LOD levels*. Distinct from
  `MeshOptions::weld_t_junctions`, which already welds intra-level T-junctions.
- Threaded meshing — `mesh_chunk` on a worker pool. The read side is already
  lock-light (`snapshot_hot`).
- A "casts AO" predicate distinct from `has_geometry` (e.g. glass doesn't occlude).

**Won't do:** D7 non-cube block-model system (engine territory); bitwise/SIMD
greedy meshing (would break the Morton layout that serves spatial queries).

---

## Pre-existing quirks (present before this session's work; left as-is)

- `HotCellAttribute::get_pitch()` / `get_yaw()` return values in *shifted
  bit-space* (matching the pre-shifted `Pitch` / `Yaw` enum constants), not
  `0..3`. The face-brightness getters were buggy the same way — fixed; pitch/yaw
  intentionally left.
- Several classes have an empty `private:` immediately followed by `public:`
  (`HotCellAttribute`, `BlockBuilder`, …) — harmless, original style.
- `inspect.hpp`'s `group_inspect` / `IsInspect` is heavy variadic
  metaprogramming; `BlockRegistry::inspect_block` / `inspect_blocks` are the only
  users. `BlockRegistryBuilder::build()` had a real double-fill bug — fixed
  (`test_block.cpp`).
- `types.hpp` defines `in` / `un` (for `int` / `unsigned int`) — unused so far.
