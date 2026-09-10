# STATE — working notes for the next agent

Practical handoff notes: what's built, how to build/test it, and the gotchas that
will otherwise cost you an hour. For the *design*, read `ARCHITECTURE_SPEC.md`;
for the *backlog*, `REMAINING_TASKS.md`; for phase-by-phase rationale,
`docs/plans/`.

---

## Where things are

| File | What |
|------|------|
| `README.md` | product pitch + the 4-subsystem design narrative (aspirational; the source spec) |
| `ARCHITECTURE_SPEC.md` | **as-built** reference — every header, every locked decision, the decision table |
| `REMAINING_TASKS.md` | live backlog; all 4 phases are `complete`, the rest is cross-cutting |
| `docs/plans/phase-{2,3,4}-*.md` | the per-phase implementation plans (executed) |
| `docs/plans/design-followups.md` | the 10 design decisions from the design review + triggers for the deferred ones |
| `inc/cellulose/*.hpp` | the library (header-only) |
| `src/main.cpp` | the minimal voxel game demo (the only `.cpp` in `src/`) |
| `tests/test_*.cpp` | doctest cases, auto-globbed |

There is no `CLAUDE.md`; this file is the closest thing. `.superpowers/` and the
superpowers plugin were removed / disabled — don't look for them.

---

## Current state

- **All four README subsystems are implemented and tested**: core data structure
  (Phase 1), chunk-level concurrency (Phase 2), spatial querying (Phase 3),
  greedy meshing + LOD (Phase 4). Plus the design follow-ups (extensible hot
  type, `Chunk::revision`, mesher rule split, opt-in `shared_ptr` chunk storage,
  strict atomics + sharded directory from the benchmarks, opt-in ambient
  occlusion) and texture management (per-face `TextureID` on `BlockRegistry`,
  `texture_id` in the merge key, `TextureAtlas` / `atlas_builder`, raylib
  atlas-tiling bridge — `docs/plans/texture-management.md`).
- **88 tests, all green.** `main` == `origin/main`.
- Headers (`inc/cellulose/`): `types coordinate morton cell block texture
  atlas_builder inspect sync seqlock rwlock chunk world cursor vector raycast
  volume collision mesh` + `cellulose.hpp` (umbrella) + `raylib.hpp` (opt-in,
  **not** in the umbrella).

---

## Build & run

Default toolchain here is **MSVC `cl.exe` via the Visual Studio generator** (not
clang — despite what old notes say). C++20.

```sh
cmake -S . -B build          # ~2-3 min the first time (raylib builds from FetchContent)
cmake --build build          # incremental builds are seconds
ctest --test-dir build       # <3 s; 84 cases
```

- Test binary: `build/tests/<Config>/cellulose_tests.exe`. Demo:
  `build/<Config>/cellulose.exe` (multi-config generator → `Debug/` subdir). The
  demo (`src/main.cpp`) is a **minimal voxel game** — fly camera, hold-LMB to
  mine, RMB place, per-face **textures** via a procedural atlas + the atlas-tiling
  shader, baked **ambient occlusion** (`MeshOptions`), and a **custom cold-tier
  attribute** (`struct Damage`) via `Chunk<HotCellAttribute, PackedChunkAttributes<Damage>>`
  + `read_cold` / `write_cold` — gated behind `CELLULOSE_BUILD_DEMO` (ON by default).
- **The demo opens a raylib window and blocks.** To smoke-check in a script:
  run detached, `sleep 5`, then `taskkill //F //IM cellulose.exe`. Expect
  `world: 9 chunks generated` on stdout, two custom shaders compiled, 9 meshes
  uploaded, no GL errors.
  **Stray `cellulose.exe` processes from earlier runs will hold `build/` locked**
  (`rm -rf build` fails "Device or resource busy") — `taskkill //F //IM cellulose.exe` first.
- CI (`.github/workflows/`) **builds only** on Linux/macOS/Windows and **does not
  run ctest**. `lint.yml` runs `clang-format` on **`src/**` only**. So a broken
  test or an `inc/` format slip passes CI — check locally.

### Sanitizers

`-DCELLULOSE_SANITIZER=<address|thread|undefined>` on `cmake` → passes
`/fsanitize=` (MSVC) or `-fsanitize=` (GCC/Clang) to `cellulose_tests`.

- **ThreadSanitizer: not supported on Windows at all.** Only Linux/macOS + GCC/Clang.
- **ASan on MSVC builds and links, but the exe won't start** without
  `clang_rt.asan_*.dll` on `PATH` (exits `0xC0000135`, ctest hangs). Needs the
  MSVC ASan runtime dir on PATH — treat ASan as a CI/Linux concern.
- The portable signal is the threaded **stress tests** (they fail deterministically
  if you remove the lock they're testing — verified during Phase 2).

### GCC / Linux vs MSVC

The dev machine is MSVC — MSVC is lenient about missing transitive includes and
some overload resolution. The GitHub Linux/macOS CI (GCC/Clang, **LP64**) is
stricter. Two classes of thing that bit here and will bite again:

- **`<shared_mutex>` does not pull `<mutex>`** on libstdc++. Any header using
  `std::unique_lock` / `std::lock_guard` must `#include <mutex>` explicitly.
- **`uint_fast16_t == uint_fast32_t == uint_fast64_t == unsigned long` on LP64
  Linux** — they collapse. This made `libmorton`'s `uint_fast*`-templated LUT
  overloads ambiguous, so `libmorton` was **dropped** — `morton.hpp` is now a
  self-contained magic-bits interleave. Don't reintroduce a `uint_fast*`-heavy
  templated dependency without checking it on Linux.

---

## Gotchas (the ones that will bite)

1. **`impl::` name lookup.** Inside `namespace cellulose::impl`, the bare names
   `SeqLock` / `RWLock` / `Padded` / `HotCellAttribute` bind to the *unspecialised
   class templates* (`impl::SeqLock` etc.), **not** the `cellulose::` aliases. You
   must write `cellulose::SeqLock`, `cellulose::HotCellAttribute`, … in `impl`
   code. This has bitten every phase. Symptom: `C2955 / C7602: use of class
   template requires template argument list`.

2. **doctest test names must not contain `;`.** `doctest_discover_tests` passes
   names through CMake, which splits on `;` (list separator) → the test silently
   splits into two entries. Use "and" / "—" instead.

3. **`Chunk` is non-movable and non-copyable** (it embeds `std::mutex` /
   `std::shared_mutex` / `std::atomic`). `World` therefore stores
   `unique_ptr<Chunk>` (default) or `shared_ptr<Chunk>` (`ChunkStorage::Shared`).
   Any container of `Chunk` by value won't compile.

4. **`clang-format` include ordering.** `.clang-format` has `IncludeBlocks:
   Preserve` + `SortIncludes` on. Put the header-under-test in **its own
   paragraph** right after `<doctest/doctest.h>`, then a blank line, then the rest
   — otherwise it sorts `<algorithm>` above `<cellulose/foo.hpp>`. Run
   `clang-format -i` on touched files before committing; only `src/` is CI-gated
   but keep `inc/` and `tests/` clean.
   Style: **tabs**, `ColumnLimit: 0` (no wrapping), `{ a, b }` brace spacing,
   `} //namespace x` closers, trailing return types, `p_`-prefixed params, no
   `m_` on public members. `.clang-format` says `Standard: c++17` but the code is
   C++20 (concepts, `std::atomic_ref`, `std::default_initializable`).

5. **Hot tier is `std::atomic_ref` by default** (D3 adopted). `chunk.hpp`
   auto-defines `CELLULOSE_STRICT_ATOMICS` unless `CELLULOSE_LOOSE_ATOMICS` is
   set. Consequences: a **`write_hot` closure must assign whole elements**
   (`hot[i] = HotCellAttribute{...}`), never fields (`hot[i].block_id = ...`) —
   that's a compile error under strict; `read_hot`'s `hot` arg is an
   `AtomicReadView` (`hot[i]` yields a value, not a reference). `-DCELLULOSE_LOOSE_ATOMICS=ON`
   restores the old plain-array benign race (faster writes, TSan-flagged, UB) —
   the torn-read stress tests only compile in that config.

6. **Custom hot attribute types** must satisfy `concept HotAttribute`:
   `std::is_trivially_copyable_v` + `std::default_initializable` + a `block_id`
   member convertible to `BlockID`. That's *all* — brightness/orientation are not
   required; the mesher reads brightness through the `face_brightness(attr, face)`
   customization point and flat-shades if there's no overload.

7. **`mesh_chunk` / `mesh_chunk_lod` are overloaded on arity.**
   `mesh_chunk(world, cp, is_solid)` = opaque cubes. `mesh_chunk(world, cp,
   has_geometry, is_hidden)` = the general form (transparency/cutout). Don't add a
   defaulted 4th param — it breaks the overload distinction.

8. **`PackedCellAttributeCollection<size CellCount, typename... Attributes>`** is
   the *general* SoA-grid type; `CellCount` can't be defaulted (it precedes the
   pack). Use the `PackedChunkAttributes<Attributes...>` alias (chunk-sized) when
   plugging a cold attribute into `Chunk`.

9. **`coordinate.hpp` must not include `chunk.hpp`.** It exposes
   `chunk_edge_length_shift` / `chunk_edge_length_mask` (literals `5` / `31`);
   `chunk.hpp` `static_assert`s they match `chunk_edge_length`. Keep the
   dependency one-way.

10. **`to_chunk_position` narrows to `i32`** — correct only while
    `|world_axis| < 2^(31 + shift)` (≈ ±2^36 blocks). Fine for any real world;
    don't be surprised at pathological coordinates.

11. **Line endings.** `git config core.autocrlf = true` here → git warns "LF will
    be replaced by CRLF" on every commit touching a text file. Harmless; the repo
    stores LF.

12. **`raylib.hpp` is not in the umbrella** and includes `<raylib.h>`. Only
    `src/main.cpp` uses it. `libcellulose` (the INTERFACE lib) does **not** link
    raylib — only the `cellulose` executable does. Don't add raylib back to the
    INTERFACE target.

13. **Texture ids in the greedy merge key.** `greedy_mesh` keys on
    `(resolved-texture-id, brightness)`, **not** `block_id` — `block_id` is
    carried in a parallel `block_at` array and copied from the run's origin cell.
    `texture_id == 0` is the "unset" sentinel → falls back to `block_id`, so real
    texture ids start at 1 (and `atlas_builder::pack_grid` reserves cell 0).
    `mesh_chunk`'s 4-arg `(is_solid, texture_of)` vs `(has_geometry, is_hidden)`
    overloads are disambiguated by `impl::FaceTextureResolver` (a resolver
    returns exactly `TextureID` from `(attr, i32)`) — don't make a texture
    resolver return `auto`/`u32`-that-isn't-`TextureID` or it won't be picked.

14. **The atlas bridge is a uniform-grid tiling shader.** `to_raylib_mesh(mesh,
    atlas)` bakes `atlas.rect_of(texture_id).min` into `texcoords2`; the shader
    does `origin + fract(tileUV) * uTileSize` with `uTileSize` a single uniform
    (from `atlas.rect_of(0)` extent). Non-uniform packed atlases need a custom
    shader. Use `NEAREST` filter + `CLAMP` wrap on the texture.

15. **`World` directory is sharded** (`shard_count = 16`, `world.hpp`). Single-shard
    ops route by `ChunkPositionHash & 15`; `for_each_chunk` / `chunk_count` touch
    all shards — `for_each_chunk` holds `shard_count` shared locks at once, so a
    visitor that calls back into `world.chunk()` deadlocks (same hazard as the
    old single mutex). `sample_chunk` reads the centre chunk via one
    `Chunk::snapshot_hot` and the apron via `ChunkCursor` — two paths on purpose.

16b. **`impl::face_layout[6]`** is the single source of truth for per-face merge
    axes, texture orientation (side faces: `v=0` at world `+Y`, `u` left-to-right
    from outside) and triangle winding. `emit_quad` takes a `FaceLayout`;
    `sample_chunk`'s AO corner sampling reads the same table. Change the
    convention *there*, not in scattered `(axis+1)%3` arithmetic.

16. **`MeshOptions`** is the trailing arg on every `mesh_chunk` / `mesh_chunk_lod`
    — `{ .ambient_occlusion, .weld_t_junctions }`, both opt-in, both leave
    geometry byte-identical when off. Adding it forced positive concept
    constraints — `impl::FaceHiddenRule` on the `(has_geometry, is_hidden)`
    overloads and `impl::FaceTextureResolver` on the both-rules-plus-texture ones
    — so `MeshOptions` in the 4th/5th slot isn't mistaken for an `is_hidden` or a
    resolver. Don't remove those constraints. AO: level-0 only, occluder test is
    `has_geometry`. `weld_t_junctions`: a post-pass over `greedy_mesh`'s output
    (quads are 4 verts + 6 indices), **centroid** fan on split quads (a corner
    fan slivers), unsplit quads verbatim; the demo also sets
    `FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT` (tearing + silhouette aliasing).

---

## TODOs / backlog

`REMAINING_TASKS.md` is authoritative. Summary of what's *actionable* vs *parked*:

**Actionable, no blocker:**
- Phase 1 close-out: a consolidated whole-branch review (optional — per-task
  reviews were clean).
- `ChunkMeshCache` — optional module: dirty set keyed by `ChunkPosition` using
  `Chunk::revision()`, remesh dirty chunks + their 6 face neighbours.
- LOD seam stitching (skirts between adjacent-level chunks).
- Threaded meshing (`mesh_chunk` on a worker pool — read side is already
  lock-light via `Chunk::snapshot_hot`).

**Adopted from the benchmark verdicts:** D3 (strict `atomic_ref` hot tier — now
the default, gotcha 5) and D5 (sharded `World` directory — `world.hpp`,
`shard_count` shards).

**Decided, deferred — do NOT start without the trigger** (see
`docs/plans/design-followups.md`):
- D4 CAS single-writer seqlock (drop the per-tier `std::mutex`) — trigger: a
  profile showing the writer mutex is hot.
- Generational chunk handle (replace `shared_ptr` under `ChunkStorage::Shared`) —
  trigger: streaming engines adopting `Shared` widely (B5: ~25% lookup cost).
- D10 `sweep_aabb` (continuous collision) + sphere/capsule casts — trigger: an
  actual use for fast movers / shape casts.
- D6 ambient occlusion — if built, it's an opt-in flag with AO in the merge key,
  never default. Otherwise it's the consumer's concern.

**Won't do:** D7 non-cube block shapes / a block-model system — that belongs in
the consumer's engine, not a voxel library.

---

## Pre-existing quirks (present before this session's work; left as-is)

- `HotCellAttribute::get_pitch()` / `get_yaw()` return values in *shifted
  bit-space* (matching the pre-shifted `Pitch` / `Yaw` enum constants), not
  `0..3`. The face-brightness getters *were* buggy the same way — those are fixed;
  pitch/yaw are intentionally left.
- Several classes have an empty `private:` immediately followed by `public:`
  (`HotCellAttribute`, `BlockBuilder`, …) — harmless, original style.
- `inspect.hpp`'s `group_inspect` / `IsInspect` is heavy variadic
  metaprogramming; `BlockRegistry::inspect_block` / `inspect_blocks` are the only
  users. `BlockRegistryBuilder::build()` had a real bug (double-filled the store)
  — fixed this session (`test_block.cpp`).
- `types.hpp` defines `in` / `un` (for `int` / `unsigned int`) — unused so far.
