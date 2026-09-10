# Phase 2 — Concurrency & Thread Safety — Implementation Plan

**Goal:** Make `World` / `Chunk` safe to read and write from multiple threads at
**chunk-level granularity**, using lightweight optimistic primitives, without
regressing the single-threaded API. This is the substrate the meshing pipeline
(Phase 4) and any threaded world-gen will build on.

**Spec:** `ARCHITECTURE_SPEC.md` §2 (design intent, verbatim from `README.md`
§ "Concurrency and Thread Safety"). This plan fills in every concrete decision
the spec leaves open and records them under *Decisions* below — correct any of
them before/while executing.

**Scope in:** chunk-storage stability (C1), a `SeqLock` primitive, a read-write
lock primitive, embedding both in `Chunk`, a world-directory lock, cache-line
alignment / false-sharing padding, and a threaded stress-test suite.

**Scope out (deferred — see end):** reclaiming a chunk while readers may still
hold a pointer to it (thread-safe *unload*); a lock-free world directory; NUMA
placement; work-stealing / job-system design.

---

## Context

After Phase 1 the data structures are correct but single-threaded:

- `World` stores `ankerl::unordered_dense::map<ChunkPosition, ChunkType, ChunkPositionHash>`
  — value-stored ~131 KB chunks. Any `try_emplace` that rehashes **moves every
  chunk**, invalidating every outstanding `Chunk*` / `HotCellAttribute*` (**C1**).
- `Chunk` hands out bare `HotCellAttribute&` / collection refs with no
  synchronisation.
- `unordered_dense` v4.9.2 is vendored and **does** provide `segmented_map` and
  `segmented_vector` (stable addresses across insert; **not** across erase).
- Toolchain: C++20, clang 21 / MSVC STL, CMake ≥ 3.10. `<thread>`,
  `<atomic>`, `<shared_mutex>`, `<new>` (`std::hardware_destructive_interference_size`)
  are all available; MSVC STL needs no extra link, but the plan links
  `Threads::Threads` for portability.

---

## Decisions (invented here — verify before executing)

| # | Question | Decision | Rationale / alternatives |
|---|----------|----------|--------------------------|
| D1 | How to stabilise chunk addresses (C1)? | `World` stores `ankerl::unordered_dense::map<ChunkPosition, std::unique_ptr<ChunkType>, ChunkPositionHash>`. Accessors return `ChunkType*` into the heap object. | The chunk is ~131 KB; an 8-byte indirection + one allocation per chunk is negligible, and dense pointer iteration is preserved. **Stable across both insert and erase** (only the `unique_ptr` slot moves, never the pointee). `segmented_map` was rejected: it is stable across insert but **not erase**, so unloading chunk B could still move chunk C. |
| D2 | Who protects the `World` map itself? | A `std::shared_mutex` (`World::m_directory_mutex`) guarding **only the directory** (`m_chunks`): shared for `has_chunk` / `find_chunk` / `chunk_count` / `for_each_chunk` iteration, exclusive for `chunk()` insert and `remove_chunk`. A looked-up `ChunkType*` stays valid after the world lock is released (until that chunk is unloaded — see deferred). | Lookups dominate and are O(1)+brief, so directory contention is low. This is *directory*-level locking, not *voxel-data*-level — it does not contradict README's "chunk-level granularity" for voxel data. |
| D3 | Primitive for hot + cold (packed) data? | A **seqlock** (`impl::SeqLock<>`): `std::atomic<u64>` sequence counter, `write()` bumps odd→write→even with release ordering, `read(fn)` retries `fn` until the counter is stable and even (acquire ordering). Safe because `m_hot` (`std::array`) and every packed SoA array are **fixed-size** — a torn read touches valid storage and is simply retried. | README: "optimistic, unblocked reads … eliminate write starvation". Writers never block. |
| D4 | One seqlock for hot+cold, or one each? | **One each** — `Chunk::m_hot_lock` and `Chunk::m_cold_lock`. | Mirrors the SoA rationale: cold-tier write traffic must not force hot-tier readers (the meshing hot path) to retry. Two `u64` atomics is cheap. |
| D5 | Seqlock write mutual exclusion? | The seqlock serialises readers against a writer but **not writers against each other**. `Chunk` pairs each seqlock with a `std::mutex` (`m_hot_writer_mutex` / `m_cold_writer_mutex`) that `write()` takes first. | Keeps the primitive simple and correct; multi-writer is real (world-gen + edits). If profiling shows the writer mutex is hot, revisit with a CAS-based single-writer claim. |
| D6 | Primitive for freezing-cold (sparse) data? | `impl::RWLock<>` — a thin wrapper over `std::shared_mutex` exposing `read(fn)` / `write(fn)` scoped-guard helpers. **Not** a seqlock: the sparse `unordered_dense::map` can rehash/grow, so a concurrent reader could touch freed storage — this tier needs real mutual exclusion. | README: "Read-Write Locks (Freezing Cold Data) … write starvation risks remain minimal" (rare access). |
| D7 | Concurrent accessor API shape? | Functor-based, added **alongside** the existing bare accessors (which stay, re-documented "single-threaded / caller holds the lock"): `Chunk::read_hot(fn) const`, `write_hot(fn)`, `read_cold` / `write_cold`, `read_sparse` / `write_sparse`. `fn` receives a ref to the underlying storage (`const HotStorage&` etc.). Return value forwarded. | Non-breaking; the closure scope *is* the critical section, which is hard to misuse. A full replacement is out of scope until the meshing API is designed. |
| D8 | Cache-line size constant? | `inline constexpr size cellulose::cache_line_size` = `std::hardware_destructive_interference_size` when `__cpp_lib_hardware_interference_size` is defined, else `64`, with `static_assert(cache_line_size >= alignof(std::max_align_t))`. Lives in a new `inc/cellulose/sync.hpp` (or `cache.hpp`). | GCC warns on direct use of the interference-size constant across TUs; a single pinned constant is ABI-stable. |
| D9 | Chunk alignment? | `impl::Chunk` gets `alignas(cache_line_size)`. Each embedded lock group (`{seq counter, writer mutex}` and the `shared_mutex`) is wrapped in an `alignas(cache_line_size)` member (or a `Padded<T>` helper) so no two locks — and no lock and the voxel arrays — share a line. | README "Eliminating False Sharing". `std::make_unique<Chunk>` honours over-alignment in C++17+. |
| D10 | Where do the primitives live? | New headers `inc/cellulose/seqlock.hpp`, `inc/cellulose/rwlock.hpp`, `inc/cellulose/sync.hpp` (cache constant + `Padded<T>`). Flat layout, matching the existing `inc/cellulose/*.hpp`. Added to `cellulose.hpp`. | Consistent with the repo. |
| D11 | `remove_chunk` contract during Phase 2? | Unchanged: **caller guarantees no other thread is accessing that chunk** while it is removed. Documented, `static_assert`-free. | Safe reclamation under live readers is a separate, larger design (deferred). |

---

## Global Constraints

- Follow every convention in `ARCHITECTURE_SPEC.md` § "Conventions" (`impl::`
  templates + aliases, no `m_` on public members, `p_` params, trailing returns,
  tabs, `CEL_*_HPP` guards, attribution trailers, one commit per task).
- **Header-only** — `libcellulose` stays `INTERFACE`; all new code in
  `inc/cellulose/`.
- **No API regression** — every existing test in `tests/` keeps passing
  unchanged; bare accessors keep their current signatures.
- **C++20**, `std::atomic` / `std::shared_mutex` / `std::mutex` only — no
  platform intrinsics, no third-party concurrency lib.
- TDD per task: RED (failing test / race repro) → GREEN → commit.
- Threaded tests must fail *without* the synchronisation and pass *with* it under
  a high iteration budget (≥ 1e6) plus `std::this_thread::yield()` injection —
  this is the portable signal, and it runs on the dev machine (Windows/clang).
- `CELLULOSE_SANITIZER` cache option passes the sanitizer flag through to
  `cellulose_tests` (`/fsanitize=` on MSVC, `-fsanitize=` elsewhere). The dev
  machine's default toolchain is **MSVC cl.exe** (VS generator), which supports
  only `address`. **ThreadSanitizer is Linux/macOS + GCC/Clang only** — run
  `-DCELLULOSE_SANITIZER=thread` in Linux CI or a Linux checkout.

---

## File Structure

**Created:**
- `inc/cellulose/sync.hpp` — `cache_line_size`, `Padded<T>` alignment helper.
- `inc/cellulose/seqlock.hpp` — `impl::SeqLock<>` + `cellulose::SeqLock` alias.
- `inc/cellulose/rwlock.hpp` — `impl::RWLock<>` + `cellulose::RWLock` alias.
- `tests/test_sync.cpp` — `Padded` alignment / size asserts.
- `tests/test_seqlock.cpp` — single-thread semantics + threaded torn-read stress.
- `tests/test_rwlock.cpp` — mutual-exclusion + concurrent-reader semantics.
- `tests/test_world_concurrency.cpp` — pointer stability, directory-lock races,
  chunk read/write races (all TSan-clean).

**Modified:**
- `inc/cellulose/world.hpp` — `unique_ptr` storage (D1), directory `shared_mutex`
  (D2), get-or-create fix, `find_*` return stable pointers, locked
  `for_each_chunk`.
- `inc/cellulose/chunk.hpp` — embed `m_hot_lock` / `m_cold_lock` (seqlock +
  writer mutex) and `m_sparse_lock` (rwlock), `alignas`, functor accessors (D7),
  re-doc bare accessors.
- `inc/cellulose/cellulose.hpp` — add the three new headers.
- `CMakeLists.txt` / `tests/CMakeLists.txt` — `find_package(Threads)`, link
  `Threads::Threads`; `CELLULOSE_SANITIZER` option wiring `-fsanitize=`.
- `ARCHITECTURE_SPEC.md` §2 — replace "designed, not built" with the as-built
  primitives + the locking contract; add D1–D11 to the locked-decision table.
- `REMAINING_TASKS.md` — tick Phase 2 items; move unload-safety to backlog.

---

## Task 1 — `Threads` + sanitizer build wiring

**Files:** `CMakeLists.txt`, `tests/CMakeLists.txt`.

- [ ] `find_package(Threads REQUIRED)`; `target_link_libraries(libcellulose INTERFACE Threads::Threads)`.
- [ ] Add `option`/`set(CELLULOSE_SANITIZER "" CACHE STRING "one of: '', thread, address, undefined")`;
      when non-empty, add `-fsanitize=${CELLULOSE_SANITIZER}` (+ `-g -fno-omit-frame-pointer`)
      to `cellulose_tests` compile+link.
- [ ] **Verify:** `cmake -S . -B build` clean; `cmake -S . -B build-tsan -DCELLULOSE_SANITIZER=thread`
      configures; existing suite still 21/21 in both.
- [ ] Commit: "Wire Threads::Threads and a CELLULOSE_SANITIZER build option."

## Task 2 — `sync.hpp`: cache-line constant + `Padded<T>`

**Files:** create `inc/cellulose/sync.hpp`, `tests/test_sync.cpp`.

- [ ] RED: `test_sync.cpp` — `static_assert(alignof(cellulose::Padded<int>) >= cellulose::cache_line_size)`;
      `CHECK(cellulose::cache_line_size >= 64)`; `Padded<T>` forwards construction and
      `operator*` / `operator->` to the wrapped `T`.
- [ ] GREEN: `cache_line_size` per D8; `template <typename T> struct alignas(cache_line_size) Padded { T value; … };`
      (perfect-forwarding ctor, deref ops, implicit `T&` conversion).
- [ ] Commit: "Add `sync.hpp` with `cache_line_size` and `Padded<T>`."

## Task 3 — `SeqLock` primitive

**Files:** create `inc/cellulose/seqlock.hpp`, `tests/test_seqlock.cpp`.

**Interface (`impl::SeqLock<>`, aliased `cellulose::SeqLock`):**
- `template <typename WriteFn> auto write(WriteFn &&) -> void` — bump to odd
  (`release`), run fn, bump to even (`release`). **Not** internally serialised —
  caller holds a writer mutex (see `Chunk`).
- `template <typename ReadFn> auto read(ReadFn &&) const -> std::invoke_result_t<ReadFn>` —
  loop: load seq `acquire`; spin while odd; run fn; load seq `acquire` again;
  retry if changed. Returns fn's result (fn must be pure / side-effect-free on
  retry).
- `auto sequence() const -> u64` (test hook).

- [ ] RED (single-thread): interleaved `write` / `read` round-trips a value;
      `sequence()` is even at rest, advances by 2 per `write`.
- [ ] RED (threaded torn-read): 1 writer thread flips a `struct { u64 a, b; }` between
      `{0,0}` and `{K,K}` under the seqlock for ~1e6 iterations; N reader threads
      `read` the pair and `REQUIRE(pair.a == pair.b)`. Fails without the retry loop.
      Run under `-DCELLULOSE_SANITIZER=thread` — must be race-free (the atomic seq
      is the only cross-thread channel; the data is `std::atomic_ref` or the read
      copies under the guard — **decide**: simplest is the payload being
      trivially-copyable and the fn doing a single `memcpy`-equivalent copy, which
      TSan still flags as a data race on the payload bytes → use
      `std::atomic_ref<u64>` on the two words, or accept that the *real* `Chunk`
      case relies on fixed-size arrays + `acquire`/`release` and use a
      `relaxed`-atomic payload in this microtest). Record the choice in the test
      file header.
- [ ] GREEN + Commit: "Add `SeqLock` optimistic-read primitive."

> **Open question for review:** strict reading of the C++ memory model says a
> non-atomic payload written under a seqlock and read racily is UB even if the
> value is discarded on retry. Production seqlock code relies on this working on
> real hardware + `volatile`/`atomic_ref`/inline-asm fences. Decide during this
> task whether `Chunk`'s hot array is accessed through `std::atomic_ref` per
> element (portable, some overhead) or via a documented
> "benign-race + acquire/release fence" approach (matches README's intent, needs
> a `static_assert(std::atomic<u64>::is_always_lock_free)` and a comment). This
> choice sets how `read_hot` is written in Task 5.

## Task 4 — `RWLock` primitive

**Files:** create `inc/cellulose/rwlock.hpp`, `tests/test_rwlock.cpp`.

- [ ] Interface: `read(fn) const` (shared lock), `write(fn)` (unique lock),
      both forwarding the fn's result; fn gets no args (captures its data).
- [ ] RED: N readers observe a consistent snapshot while 1 writer grows an
      `unordered_dense::map`; without the lock, TSan flags the rehash race and/or
      an assert trips.
- [ ] GREEN + Commit: "Add `RWLock` (shared_mutex) primitive for the sparse tier."

## Task 5 — Embed locks in `Chunk` + functor accessors

**Files:** `inc/cellulose/chunk.hpp`, `tests/test_chunk.cpp` (append),
`inc/cellulose/cellulose.hpp`.

- [ ] `alignas(cache_line_size)` on `impl::Chunk`.
- [ ] Add members: `Padded<SeqLock> m_hot_seq`, `Padded<std::mutex> m_hot_writer`,
      `Padded<SeqLock> m_cold_seq`, `Padded<std::mutex> m_cold_writer`,
      `Padded<RWLock> m_sparse_lock`. (Or group into two padded structs — decide
      for layout; keep each lock off every other lock's line.)
- [ ] `template <typename F> auto read_hot(F &&) const` → `m_hot_seq->read([&]{ return f(m_hot); })`;
      `write_hot(F &&)` → `lock_guard(m_hot_writer); m_hot_seq->write([&]{ f(m_hot); })`.
      Same for `*_cold` (over `m_packed`) and `*_sparse` → `m_sparse_lock->read/write`.
- [ ] Keep the four bare `hot_attribute` overloads + `packed()` / `sparse()` /
      `fill_hot`; doc-comment them "not synchronised — single-threaded use or
      caller-held lock only".
- [ ] `static_assert(alignof(Chunk<>) >= cache_line_size)`; `static_assert` the
      hot array offset is on its own line clear of the locks (a
      `offsetof`-style check or a comment-backed layout test).
- [ ] RED→GREEN: `test_chunk.cpp` — `write_hot` then `read_hot` round-trips;
      `read_hot` returns a computed value; alignment asserts. Threaded case goes
      in Task 6.
- [ ] Commit: "Embed hot/cold seqlocks and a sparse rwlock in `Chunk`."

## Task 6 — `World`: stable storage + directory lock

**Files:** `inc/cellulose/world.hpp`, `tests/test_world.cpp` (append),
`tests/test_world_concurrency.cpp` (new).

- [ ] Storage → `ankerl::unordered_dense::map<ChunkPosition, std::unique_ptr<ChunkType>, ChunkPositionHash>`.
- [ ] `m_directory_mutex` (`std::shared_mutex`, `mutable`).
- [ ] `chunk()`:
      ```
      { std::unique_lock g(m_directory_mutex);
        auto [it, inserted] = m_chunks.try_emplace(p_position);
        if (inserted) it->second = std::make_unique<ChunkType>();
        return *it->second; }
      ```
- [ ] `find_chunk` / `has_chunk` / `chunk_count` → `std::shared_lock`; `find_chunk`
      returns `it->second.get()` (stable). `remove_chunk` → `std::unique_lock`
      (D11 contract in the doc comment).
- [ ] `for_each_chunk` → `std::shared_lock` for the whole traversal; visitor gets
      `(const ChunkPosition&, ChunkType&)` as today.
- [ ] `find_hot_attribute` — **change semantics**: it can no longer safely return
      a bare `HotCellAttribute*` (unsynchronised). Options for review: (a) keep it
      but re-doc "single-threaded"; (b) replace with
      `template <typename F> auto with_hot_attribute(WorldPosition, F&&)` running
      `f` under the chunk's hot seqlock; (c) both. **Recommend (c)** — keep the
      pointer form for the demo/tests, add the functor form as the blessed
      concurrent path.
- [ ] RED (pointer stability): insert 10 000 chunks, keep a `Chunk*` to the first,
      write through it after every insert — value holds (fails today).
- [ ] RED (directory race, TSan): T1 spins `chunk()` on random positions, T2
      spins `find_chunk` + `chunk_count`, T3 spins `for_each_chunk`; 1 s;
      TSan-clean, no crash.
- [ ] RED (chunk-data race, TSan): many threads `world.with_hot_attribute(pos, …)`
      read/increment while others `write_hot` a fill; readers always see a
      consistent `HotCellAttribute` (block_id/state from the same write).
- [ ] GREEN + Commit: "Store chunks behind `unique_ptr` and guard the `World` directory."

## Task 7 — Docs + suite sweep

- [ ] `ARCHITECTURE_SPEC.md` §2 → as-built (primitives, the three-tier lock
      contract, D1–D11 in the decision table); note the directory-lock vs
      voxel-lock distinction; update §1.5 (`World` storage) and §1.4 (`Chunk`
      layout / alignment).
- [ ] `REMAINING_TASKS.md` — check off Phase 2; add "thread-safe chunk unload
      under live readers" + "lock-free / sharded world directory" to backlog.
- [ ] `README.md` §"Concurrency" — no change needed (design already matches);
      add a one-line "implemented" note if desired.
- [ ] Full run: `ctest` (plain) green; `ctest` under `-DCELLULOSE_SANITIZER=thread`
      green; `clang-format --dry-run --Werror` clean on all touched files.
- [ ] Commit: "Document the concurrency layer; close out Phase 2 tasks."

---

## Verification (end-to-end)

1. **Clean build (no sanitizer):** `rm -rf build && cmake -S . -B build && cmake --build build`
   — warning-free for `inc/cellulose/*`; `cellulose`, `cellulose_tests` link.
2. **Plain suite:** `ctest --test-dir build --output-on-failure` — all Phase 1 +
   Phase 2 cases pass; Phase 1 cases unchanged.
3. **Stress suite (portable):** the threaded tests run their full iteration
   budget (≥ 1e6) and pass; a temporary revert of the lock/`atomic` in any one
   primitive makes its stress test fail (assertion or crash). **TSan (Linux):**
   on a Linux checkout, `cmake -B build-tsan -DCELLULOSE_SANITIZER=thread &&
   cmake --build build-tsan && ctest --test-dir build-tsan` — zero reports.
4. **Pointer stability:** the 10 000-insert test holds a live `Chunk*` and a
   `HotCellAttribute*` across inserts *and* one unrelated `remove_chunk`.
5. **No API regression:** `git stash` the Phase 2 diff to `tests/` and confirm the
   Phase 1 test files compile and pass verbatim against the new headers.
6. **Alignment:** `static_assert(alignof(cellulose::Chunk<>) >= cellulose::cache_line_size)`
   compiles; a runtime check prints the two lock addresses and asserts they are
   ≥ `cache_line_size` apart.
7. **Format:** `clang-format --dry-run --Werror` clean.

---

## Deferred (open for a Phase 2.5 / later plan)

- **Thread-safe chunk unload.** Reclaiming a `Chunk` while another thread may hold
  a `Chunk*` needs `shared_ptr<Chunk>` + atomic swap, hazard pointers, or an
  epoch/RCU scheme. Until then `remove_chunk` keeps the D11 "caller ensures
  quiescence" contract.
- **World directory scaling.** If directory-mutex contention shows up under many
  loader threads: shard the map by `ChunkPosition` hash bits, or move to a
  concurrent hash map. Measure first.
- **Writer-vs-writer on a seqlock.** The D5 per-tier writer `std::mutex` is the
  simple choice; a CAS-claimed single-writer slot avoids the mutex if profiling
  demands it.
- **Seqlock payload & the memory model** (Task 3 open question) — if the
  `atomic_ref` route is taken for correctness, benchmark it against the
  benign-race route on the meshing hot path.
- **NUMA / allocator** placement of chunk heap blocks.
