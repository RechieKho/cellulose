# SDD ledger — plan: C:\Users\User\.claude\plans\swirling-painting-riddle.md

## Setup
- Worktree: D:\Documents\cxx_projects\cellulose\.claude\worktrees\feat-foundation-chunk-world (branch worktree-feat-foundation-chunk-world, from origin/main @ 33cf989)
- Submodules initialised (fmt 12.2.0, libmorton v0.2.13, unordered_dense v4.9.2)
- Toolchain: CMake 4.2.3, Ninja, clang++ 21.1.8 (x86_64-pc-windows-msvc) — matches parent build
- Baseline build (existing exe + raylib fetch): running in background bk3bel7fs
- Spec: README.md — design-level only (no detailed spec). Plan's Assumptions table (A1–A12) fills gaps; rulings beyond README are provisional.

## Pre-flight conflict scan

### Cross-task pairs (shared file / interface)
| Pair | producer → consumer | finding |
|------|--------------------|---------|
| T1 → T2 | T1 creates tests/test_smoke.cpp; T2 `git rm`s it | consistent (T2 explicitly deletes) |
| T1 → all test tasks | T1 globs tests/*.cpp recursively; later tasks drop in test_*.cpp | consistent |
| T2 → T6 | `*CellAttributeCollection::get()` returns ref → Chunk `packed().get<>()[i]=v` | consistent, order correct |
| T2 ↔ T3 | both edit cell.hpp (different methods) + T3 appends to test_cell.cpp | consistent, sequential |
| T4 → T5 | `LocalPosition` → morton encode/decode | consistent names |
| T4 → T6 | `LocalPosition` → Chunk accessors; T6 defines chunk_edge_length=32 (=2^5) vs T4 hardcoded 5/31 | consistent; coupling smell noted (see ruling R2) |
| T4 → T7 | `ChunkPosition/ChunkPositionHash/WorldPosition/to_chunk_position/to_local_position` → World | consistent names/types |
| T5 → T6 | `CellIndex=u32`, `encode_cell_index` → Chunk | consistent; T6 lists `decode_cell_index` as consumed but impl only uses encode (harmless) |
| T6 → T7 | `Chunk<>`, `hot_attribute(LocalPosition) const/non-const` → World `find_hot_attribute` | consistent (both const overloads provided) |
| T7 self | `for_each_chunk` binds local `chunk` shadowing member fn `chunk()`; `find_hot_attribute` local `chunk` likewise | style smell, not a conflict — leave to task review |
| T8 → all headers | umbrella includes block/cell/chunk/coordinate/inspect/morton/types/world | all exist after T4–T7 |

### Self-consistency (per task)
- T1: smoke test asserts `1+1==2` (vacuous) — transient harness bring-up, deleted in T2. Ruled acceptable (R3).
- T1: CMake target names (`libcellulose`, `cellulose_tests`), doctest module path, `doctest_discover_tests` — all correct against repo (`LIBRARY_NAME=lib${PROJECT_NAME}`, PROJECT_NAME=cellulose).
- T2 Step 6: `git add tests/test_smoke.cpp` after `git rm` will no-op/warn — deferred-minor, implementer handles.
- T3: pitch/yaw + 6-face round-trip tests valid; precedence fix is genuine red→green.
- T4: `sizeof(ChunkPosition)==12` holds on target ABI; negative shift/mask math verified against all test cases; `detail::wyhash::hash(ptr,size)` exists (unordered_dense.h:417).
- T5: `bool seen[32768]` stack (fine); u8 loop `<32` no wrap; morton max code 32767 < 32768; bijection check valid.
- T6: `Chunk<>` ~131 KB inline hot array — stack locals in tests OK on main thread; empty Packed/Sparse collections default-construct fine.
- T7: `unordered_dense::map` value-stores 131 KB Chunk; pointer/ref invalidation on insert — identity test only re-accesses same key (no insert), so valid. Deferred concern C1.

### Rulings (pre-flight)
- **R1 — commit trailers.** Plan's per-task `git commit -m "..."` examples omit the two attribution trailers, but Global Constraints + session config mandate them. Every implementer commit MUST end with:
  `Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>` / `Claude-Session: https://claude.ai/code/session_01EcNFrbE5azaaUYgRh5VKC9`
  Carried into every dispatch. Cost if wrong: trivial (trailer text), fixable.
- **R2 — coordinate.hpp hardcoded 5/31 vs chunk.hpp chunk_edge_length=32.** Plan explicitly forbids coordinate.hpp depending on chunk.hpp and mandates a comment. Accept as-is; a `static_assert` tying them together would need chunk.hpp include, which the plan rejects. Deferred: final review may suggest relocating the constant. Cost if wrong: a future edge-length change silently breaks conversions — low likelihood, caught by tests.
- **R3 — T1 vacuous smoke test.** Acceptable as transient harness verification; deleted in T2. Not product-code test debt.
- **R4 — T8 Step 2 self-contradictory text** ("append to test_world.cpp … so instead add a new file test_umbrella.cpp"). RESOLVED: create new file `tests/test_umbrella.cpp`; do NOT modify test_world.cpp. Cost if wrong: negligible.
- **R5 — ChunkPositionHash / detail::wyhash in `cellulose` namespace & reaching into `detail::`.** Plan A10 mandates it. Accept. If task review flags, adjudicate then. Cost if wrong: internal API could change across unordered_dense versions — pinned by submodule, low risk.

### Deferred concerns (for final review / next plan)
- **C1** — `unordered_dense::map` stores 131 KB `Chunk` by value; rehash moves all chunks and invalidates any held `Chunk*`/`HotCellAttribute*`. Foundation is correct for immediate-use access; concurrency/rendering plan should evaluate `segmented_map` or `unique_ptr<Chunk>` values.
- **C2** — `cellulose_tests` links raylib transitively via the INTERFACE lib though no test uses it. Consider a raylib-free core INTERFACE target later.

## Planned models
T1 sonnet · T2 haiku · T3 haiku · T4 sonnet · T5 sonnet · T6 sonnet · T7 sonnet · T8 sonnet
Task reviewers: sonnet (haiku for T2/T3). Final whole-branch review: opus.

## Progress

- Baseline: configure+build OK (exit 0), `cellulose.exe` links; only third-party warnings (stb_vorbis, fmt fopen). No tests yet. Clean `git status`. BASE commit 33cf989.
- Task 1: dispatched (implementer, sonnet) — BASE 33cf989
- Task 1: implemented commit b0fa8e9 (DONE_WITH_CONCERNS: CMake 4 rejects doctest v2.4.11 `cmake_minimum_required(3.0)` → scoped `CMAKE_POLICY_VERSION_MINIMUM 3.5` block, tag stays pinned)
- Task 1: review — ✅ spec compliant, quality Approved; no Critical/Important; ⚠️ TDD logs in report confirmed by controller (RED: `unknown target cellulose_tests`; GREEN: 1/1 pass exit 0)
- Task 1: minor (deferred): configure-time CMake deprecation warning from doctest v2.4.11 sub-project — plan-mandated (pinned tag), warning-only, ctest output pristine; final review to weigh a tag bump
- Task 1: minor (deferred): `tests/CMakeLists.txt` GLOB_RECURSE auto-compiles any future `.cpp` anywhere under `tests/` (incl. helper subdirs) into `cellulose_tests`
- Task 1: complete (commits 33cf989..b0fa8e9, review clean)
- Task 2: dispatched (implementer, haiku) — BASE b0fa8e9
- Task 2: implemented commit 3d9ed03 (DONE) — both `get()` → `T&` + `const T&` overload; test_cell.cpp added; test_smoke.cpp removed; trailers OK
- Task 2: review dispatched (haiku), diff matches brief exactly
- Task 2: review — ✅ spec compliant, Approved, zero findings; ⚠️ trailers confirmed present by controller (`git log -1 %B 3d9ed03`)
- Task 2: complete (commits b0fa8e9..3d9ed03, review clean)
- Task 3: dispatched (implementer, haiku) — BASE 3d9ed03
- Task 3: implemented commit aa43c5d (DONE) — all 6 face-brightness getters parenthesised; 2 test cases appended; trailers confirmed present; diff matches brief
- Task 3: review dispatched (haiku)
- Task 3: review — ✅ spec compliant, Approved, zero findings (bit math verified, test confirmed strong enough to catch old bug)
- Task 3: complete (commits 3d9ed03..aa43c5d, review clean)
- Task 4: dispatched (implementer, sonnet) — BASE aa43c5d
- Task 4: implemented commit 58823b2 (DONE) — coordinate.hpp + test_coordinate.cpp; primary wyhash spelling compiled (no fallback); 8/8 pass; trailers confirmed; diff matches brief
- Task 4: review dispatched (sonnet)
- Task 4: review — ✅ spec compliant, Approved; no Critical/Important; bit math independently verified on all 4 vectors incl. negatives + round-trip; wyhash entry point confirmed at unordered_dense.h:218, deterministic (hardcoded seed) so hash `!=` test is stable
- Task 4: minor (deferred): `to_chunk_position` unguarded i64→i32 narrowing — effective world size cap ±2^36 chunks*... ; a comment/assert would document the boundary
- Task 4: minor (deferred): `ChunkPositionHash` uses `detail::wyhash::hash` (internal ns) — sanctioned by plan A10; add a comment pointing at the decision
- Task 4: minor (deferred): `to_world_position` could use `+` instead of `|` for readability (equivalent here)
- Task 4: complete (commits aa43c5d..58823b2, review clean)
- Task 5: dispatched (implementer, sonnet) — BASE 58823b2
- Task 5: implemented commit 2452c6e (DONE) — morton.hpp + test_morton.cpp; 32^3 exhaustive bijection+round-trip passes; libmorton API matches brief; trailers OK; diff matches brief
- Task 5: review dispatched (haiku)
- Task 5: review — ✅ spec compliant, Approved; no Critical/Important. One "minor" (namespace-comment spacing) is a FALSE POSITIVE — repo convention is `} //namespace cellulose` without the space (cell.hpp/block.hpp); implementer matched it correctly. No action.
- Task 5: complete (commits 58823b2..2452c6e, review clean)
- Task 6: dispatched (implementer, sonnet) — BASE 2452c6e
- Task 6: implemented commit a9f7378 (DONE_WITH_CONCERNS) — chunk.hpp + test_chunk.cpp; 13/13 pass; trailers OK
- Task 6: deviation — brief's unqualified `HotCellAttribute` inside `namespace cellulose::impl` binds to class template `impl::HotCellAttribute` not the alias → qualified `cellulose::HotCellAttribute` at 6 sites. Controller assessment: root cause correct, fix minimal & correct (`HotCellAttribute<>` or `impl::HotCellAttribute<>` would also work). Collection default args left unqualified compile fine (`impl::PackedCellAttributeCollection<...>` == alias type).
- Task 6: review dispatched (sonnet, asked to verify the deviation)
- Task 6: review — ✅ spec compliant, Approved; no Critical/Important. Deviation independently verified: `cellulose::Chunk<>` == `impl::Chunk<>` (same specialization), so Task 7 unaffected. Empty-collection defaults instantiate cleanly (A8).
- Task 6: minor (deferred): qualification asymmetry — `cellulose::HotCellAttribute` qualified while sibling consumed names bare (all correct, reads inconsistent)
- Task 6: minor (deferred): default template args restated on both `impl::Chunk` and `cellulose::Chunk` alias (only alias defaults reachable by consumers)
- Task 6: minor (deferred): test gaps — no `const Chunk` constructed so 4 const overloads never instantiated; `hot_attribute(CellIndex)` never write-tested. Low risk (1-line mirrors). Consider a `const auto& c = chunk` spot check in a later task.
- Task 6: minor (deferred): `LocalPosition` axes raw `u8`, value >=32 → OOB Morton index + UB in `m_hot[]` (upstream/out-of-scope; brief constrains domain)
- Task 6: complete (commits 2452c6e..a9f7378, review clean)
- Task 7: dispatched (implementer, sonnet) — BASE a9f7378
- Task 7: implemented commit 9d204fb (DONE) — world.hpp + test_world.cpp; full suite 18/18; trailers OK; diff matches brief
- Task 7: deviation — `find_hot_attribute` both overloads qualified `cellulose::HotCellAttribute *` (same impl-namespace lookup issue as Task 6, brief's note authorizes this fallback); `Chunk<>` left bare (== `impl::Chunk<>`)
- Task 7: review dispatched (sonnet)
- Task 7: review — ✅ spec compliant, Approved; no Critical/Important; library semantics (erase count, try_emplace get-or-create, pair<Key,T> iteration) verified against vendored header; deviation verified necessary & consistent with chunk.hpp
- Task 7: minor (deferred, FIX-BEFORE-MERGE candidates): (a) world.hpp:51 `std::forward<Visitor>` re-forwarded each loop iteration — move-only rvalue visitor footgun; fix = `p_visitor(position, chunk)`. (b) world.hpp:17 `using ChunkMap` is public — leaks container type; make private. (c) const overloads (`find_chunk`/`find_hot_attribute` const) never instantiated — Task 8 umbrella test will add a `const World&` check to close this.
- Task 7: minor (deferred): world.hpp:55,63,50 local/binding `chunk` shadows member fn `chunk()` (legal, readability)
- Task 7: minor (deferred): world.hpp:14 doc comment "Robin-Hood hash table" inaccurate (unordered_dense is dense/open-addressing) — though matches README's own wording
- Task 7: complete (commits a9f7378..9d204fb, review clean)
- Task 8: dispatched (implementer, sonnet) — BASE 9d204fb
- Task 8: implemented commit 166f51f (DONE) — cellulose.hpp umbrella + test_umbrella.cpp (2 cases incl. const-World path) + main.cpp World demo (std::endl) + README status note (no bogus in-repo path); full suite 20/20; demo stdout captured (`chunks loaded: 1` / `block at (1,2,3): 42`); trailers OK
- Task 8: review dispatched (sonnet)
- Task 8: review — ✅ spec compliant, Approved; no Critical/Important; both probed risks (const-overload instantiation, demo pointer validity) verified sound
- Task 8: minor (deferred): main.cpp:5 redundant `#include <cellulose/inspect.hpp>` (umbrella already pulls it; demo doesn't use it)
- Task 8: minor (deferred): README.md missing trailing newline (pre-existing)
- Task 8: minor (deferred): test_umbrella.cpp case 1 unguarded `->block_id` deref (case 2 guards with REQUIRE; inconsistent)
- Task 8: complete (commits 9d204fb..166f51f, review clean)

## All tasks complete — proceeding to final whole-branch review
Branch range: 33cf989..166f51f (8 task commits)
Final review model: opus. Deferred-minor list handed to it (esp. Task 7 a/b: for_each_chunk forward-in-loop + public ChunkMap; Task 1: doctest deprecation warning) + parked concerns C1/C2.
