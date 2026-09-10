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
| `src/main.cpp` | the raylib demo (the only `.cpp` in `src/`) |
| `tests/test_*.cpp` | doctest cases, auto-globbed |

There is no `CLAUDE.md`; this file is the closest thing. `.superpowers/` and the
superpowers plugin were removed / disabled — don't look for them.

---

## Current state

- **All four README subsystems are implemented and tested**: core data structure
  (Phase 1), chunk-level concurrency (Phase 2), spatial querying (Phase 3),
  greedy meshing + LOD (Phase 4). Plus the design follow-ups (extensible hot
  type, `Chunk::revision`, mesher rule split, opt-in `shared_ptr` chunk storage).
- **70 tests, all green.** `main` == `origin/main`.
- Headers (`inc/cellulose/`): `types coordinate morton cell block inspect sync
  seqlock rwlock chunk world cursor vector raycast volume collision mesh` +
  `cellulose.hpp` (umbrella) + `raylib.hpp` (opt-in, **not** in the umbrella).

---

## Build & run

Default toolchain here is **MSVC `cl.exe` via the Visual Studio generator** (not
clang — despite what old notes say). C++20.

```sh
cmake -S . -B build          # ~2-3 min the first time (raylib builds from FetchContent)
cmake --build build          # incremental builds are seconds
ctest --test-dir build       # <3 s; 70 cases
```

- Test binary: `build/tests/<Config>/cellulose_tests.exe`. Demo:
  `build/<Config>/cellulose.exe` (multi-config generator → `Debug/` subdir).
- **The demo opens a raylib window and blocks.** To check its stdout in a script:
  run detached, `sleep 4`, then `taskkill //F //IM cellulose.exe`. Expect
  `vertices: 304` / `triangles: 152`. **Stray `cellulose.exe` processes from
  earlier runs will hold `build/` locked** (`rm -rf build` fails with "Device or
  resource busy") — `taskkill //F //IM cellulose.exe` first.
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
   C++20 (concepts, `<atomic_ref>`-free but uses `std::default_initializable`).

5. **The seqlock is a documented benign race.** `Chunk::read_hot` / `read_cold`
   read the plain arrays while a writer writes plain; the retry discards torn
   reads. Sound on x86/ARM for the fixed-size arrays, **UB by the standard**,
   **TSan will flag it**. Don't "fix" it without reading design decision D3
   (`docs/plans/design-followups.md`) — the plan is a compile-time `atomic_ref`
   opt-in, gated on a benchmark.

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

---

## TODOs / backlog

`REMAINING_TASKS.md` is authoritative. Summary of what's *actionable* vs *parked*:

**Actionable, no blocker:**
- Phase 1 close-out: a consolidated whole-branch review (optional — per-task
  reviews were clean).
- `ChunkMeshCache` — optional module: dirty set keyed by `ChunkPosition` using
  `Chunk::revision()`, remesh dirty chunks + their 6 face neighbours.
- Texture-atlas UVs (`block_id → atlas rect`) — needs `Block` to carry texture
  data first.
- Bulk per-chunk `read_hot` — one seqlock acquisition + sub-range copy instead of
  per-cell, for `for_each_cell_in_aabb` / the mesher apron.
- LOD seam stitching (skirts between adjacent-level chunks).
- Threaded meshing (`mesh_chunk` on a worker pool — it only needs read access).

**Decided, deferred — do NOT start without the trigger** (see
`docs/plans/design-followups.md`):
- D3 seqlock `atomic_ref` path — trigger: a meshing-hot-path benchmark, or a need
  for TSan-clean CI.
- D4 CAS single-writer seqlock (drop the per-tier `std::mutex`) — trigger: a
  profile showing the writer mutex is hot.
- D5 shard the world directory mutex — trigger: a profile showing contention.
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
