# Concurrency Benchmarks — Plan

**Goal:** measure `cellulose` under concurrent read/write load so we can (a) know
the real throughput / latency of each lock, (b) confirm correctness holds under
sustained contention, and (c) get the numbers the deferred design decisions
(D3 seqlock `atomic_ref`, D4 CAS writer, D5 directory sharding) are gated on.

**Not goals:** micro-optimising anything yet; a CI pass/fail gate (benchmarks
measure, they don't assert); cross-machine absolute comparisons.

---

## Context — what's under test

| Surface | Primitive | Question |
|---------|-----------|----------|
| `Chunk` hot / cold tiers | `SeqLock` + per-tier writer `std::mutex` | read throughput & retry rate vs writer pressure; writer-vs-writer scaling (**D4**); benign-race vs `atomic_ref` cost (**D3**) |
| `Chunk` sparse tier | `RWLock` = `std::shared_mutex` | reader parallelism; writer stall |
| `World` directory | one `std::shared_mutex` | lookup throughput vs concurrent load/unload; contention onset (**D5**) |
| `ChunkStorage::Shared` | `shared_ptr` handles | `find_chunk` refcount overhead vs `Unique` |
| `ChunkCursor` | pointer caching | speedup on a sequential walk; behaviour vs concurrent unload |
| End-to-end | all of the above | "meshing while editing" holds up |

---

## Decisions (verify before executing)

| # | Question | Decision | Rationale |
|---|----------|----------|-----------|
| BD1 | Framework | **Hand-rolled harness**, no dependency, in `benchmarks/`. | The interesting workloads are *asymmetric* (N readers + M writers running different loop bodies); Google Benchmark's threading runs one body on N threads and fits this poorly. A ~150-line harness (fixed-wall-time roles, atomic op counters, latency histogram) covers it. GBench stays an option if we later want auto-tuned iteration counts + statistics. |
| BD2 | Build | `option(CELLULOSE_BUILD_BENCHMARKS OFF)`; `cellulose_benchmarks` exe built only when ON. **Release only** — the harness `main` aborts if built without optimisation. | Benchmarks are noise in a Debug build; keep them out of the default + CI build. |
| BD3 | Seqlock instrumentation | `option(CELLULOSE_SEQLOCK_STATS OFF)` (plumbed onto `libcellulose` as a `target_compile_definitions` — a raw `/D` gets mangled by the MSYS shell). When ON, `SeqLock::read` bumps `m_retries` (relaxed) on each discarded snapshot; `retries()` accessor. OFF = the member isn't declared and `retries()` returns 0. | Retry rate is the key seqlock health metric and can't be observed from outside the loop. **Done — T2.** |
| BD4 | `atomic_ref` variant for D3 | `option(CELLULOSE_STRICT_ATOMICS OFF)` — the **hot tier only**: `read_hot` / `write_hot` reach the array through `AtomicReadView` / `AtomicWriteView` (per-element `std::atomic_ref`; `static_assert`s the type is lock-free-sized). A strict `write_hot` closure must assign whole elements (`hot[i] = value`), not fields — the field-tearing torn-read test is `#ifndef`-guarded off under strict. Cold tier keeps the benign race (rarely hot). | Building it here means B2 measures the real cost; also the first concrete step of D3. **Done — T2.** |
| BD5 | Metrics reported | per-role ops/s (aggregate + per-thread), write/lookup latency p50 / p99 / max, seqlock retry rate, and a **correctness counter** (invariant violations — must be 0). Wall-clock, median of ≥5 runs. | Throughput alone hides tail latency and correctness regressions. |
| BD6 | Results | `benchmarks/` holds the harness + a `RESULTS.template.md`; a real run goes to `docs/benchmarks/RESULTS-<yyyy-mm-dd>-<machine>.md` (git-tracked, machine-stamped). Raw numbers are **not** committed to the plan. | Numbers are machine-specific; keep the plan portable. |
| BD7 | Cross-platform | Timing runs on the dev machine (MSVC Release). Add a **manual-dispatch** CI job (`workflow_dispatch`) that builds + runs the benchmarks on Linux for a sanity cross-check and to run the mixed workload under TSan/ASan. Not on every push. | The concurrency correctness story wants a real TSan run; timing on CI runners is noisy but directionally useful. |

---

## Harness design (`benchmarks/`)

```
benchmarks/
  CMakeLists.txt          # cellulose_benchmarks, links libcellulose, Release-guarded
  harness.hpp             # Role, run_for(duration, roles...), Histogram, Report
  main.cpp                # CLI: --scenario --readers --writers --chunks --duration-ms --csv
  bench_seqlock.cpp       # B1, B2
  bench_rwlock.cpp        # B3
  bench_world.cpp         # B4, B5
  bench_cursor.cpp        # B6
  bench_workload.cpp      # B7
```

**`harness.hpp` core:**
- `struct Role { std::string name; std::function<void(std::atomic<bool>& stop, Counters&)> body; int threads; };`
- `Report run(std::chrono::milliseconds duration, std::vector<Role> roles)` — spawns
  every thread, a 200 ms warm-up (counters reset after), spins `stop` for
  `duration`, joins, returns per-role op counts + a merged latency `Histogram`
  (HDR-lite: fixed log-spaced buckets, enough for p50/p99/max).
- `DoNotOptimize(x)` — `#if defined(_MSC_VER)` → `_ReadWriteBarrier()` + volatile
  sink; else `asm volatile("" : : "r,m"(x) : "memory")`. Every read result feeds a
  per-thread `volatile`/atomic sink so the compiler can't elide it.
- CLI parses knobs; `--csv` emits one machine-readable row per (scenario, config).
- `main` errors out if `!defined(NDEBUG)`.

**Methodology notes baked into the harness / README:**
- Report the machine (CPU, core count, OS, compiler) in every result file.
- No thread pinning by default (realistic); `--pin` optional.
- Run each config ≥5×, report median + min/max; note turbo/thermal on laptops.
- Working set sized to fit RAM — see per-scenario caps below.

---

## Scenarios

### B1 — Seqlock hot-tier: readers vs writers

- One `Chunk<>`. Writer(s) maintain the invariant `hot[i].block_id == hot[i].state`
  by writing the same incrementing value to both; a `--wide-window` flag adds
  `std::this_thread::yield()` between the two stores.
- Readers `chunk.read_hot([&]{ return hot[i]; })`, feed the snapshot to a sink,
  and count any `block_id != state` as an **invariant violation** (must stay 0).
- Grid: readers ∈ {1, 2, 4, 8}, writers ∈ {0, 1, 2, 4}.
- Report: reads/s (agg + per-reader), writes/s, write p50/p99, seqlock retry rate
  (`CELLULOSE_SEQLOCK_STATS`), violations.
- **Informs:** the read fast-path baseline; writer scaling (does writes/s collapse
  from 1→4 writers? → **D4** CAS-writer evidence).

### B2 — Seqlock hot-tier: benign race vs `atomic_ref` (D3)

- Same shape as B1 (readers=4, writers=1 and writers=2), two variants of the
  binary: default `read_hot` vs `-DCELLULOSE_STRICT_ATOMICS=ON`.
- Report: reads/s delta, writes/s delta.
- **Informs D3:** if the strict path costs < ~5% on the meshing-shaped read, make
  it the default (drops the UB + unblocks TSan-clean CI); otherwise keep opt-in.

### B3 — RWLock sparse tier

- One `Chunk<HotCellAttribute, PackedChunkAttributes<>, SparseChunkAttributes<std::array<u8, 64>>>`,
  pre-seeded with ~1000 entries.
- Readers `read_sparse` scanning the map; writers `write_sparse` inserting /
  erasing to keep the size roughly stable.
- Grid: readers ∈ {1, 2, 4, 8}, writers ∈ {0, 1, 2}.
- Report: reads/s (does it scale ~linearly with readers until a writer appears?),
  writes/s, reader stall (max gap between a reader's ops) when a writer holds the
  unique lock.
- **Informs:** whether `std::shared_mutex` is fine or the reader-count cache line
  is a bottleneck.

### B4 — World directory: lookup vs load/unload (D5)

- `World<Chunk<TinyHot>>` (`TinyHot = { BlockID block_id; }` → ~32 KB chunks, so
  the directory is what's measured, not chunk memory). Pre-load K chunks.
- Query threads: `world.find_chunk(random_loaded_pos)` → sink the pointer.
- Streamer threads: alternate `world.chunk(fresh_pos)` / `world.remove_chunk(old_pos)`
  keeping the count near K.
- Grid: K ∈ {128, 1024, 8192}, query threads ∈ {1, 4, 8, 16}, streamers ∈ {0, 1, 4}.
  8192 × 32 KB ≈ 256 MB — cap K there.
- Report: lookups/s (agg + per-thread), loads/s, unloads/s, lookup p99 / max
  (spikes while a streamer holds the exclusive lock).
- **Informs D5:** the thread count at which the single `shared_mutex` stops
  scaling → whether to shard.

### B5 — `Shared` vs `Unique` storage overhead

- B4's pure-lookup config (streamers = 0), run for `World<…, ChunkStorage::Unique>`
  and `…::Shared`.
- Report: lookups/s delta (the `shared_ptr` copy + refcount atomic per `find_chunk`).
- **Informs:** the price of the opt-in; whether a lighter generational handle is
  worth designing.

### B6 — ChunkCursor

- A raycast-shaped sequential walk over ~4096 cells that stays within a handful of
  chunks. Variant A uses `impl::ChunkCursor`; variant B calls `find_chunk` per
  cell.
- Report: cells/s, `find_chunk` calls, speedup. (Turns the anecdotal 0.29 s →
  0.06 s LOD test into a real number.)
- Correctness sub-check: run variant A while a streamer thread loads/unloads
  chunks **not** on the walk path (`ChunkStorage::Shared`) → zero crashes / zero
  wrong reads. Document that unloading a chunk *on* the path is out of contract
  under `Unique`.

### B7 — Mixed workload: "meshing while editing"

- `World<>` with ~256 chunks holding a blocky scene.
- Roles (all for a fixed 5 s wall time):
  - **Meshers** (P threads): random chunk → `mesh_chunk(world, cp, is_solid)` →
    discard. Exercises ~thousands of `read_hot` snapshots per mesh through the cursor.
  - **Editors** (Q threads): random chunk + cell → `write_hot` toggling a block id.
  - **Queriers** (R threads): `raycast` / `move_aabb` from random origins.
- Grid: (P, Q, R) ∈ { (4,1,0), (4,2,2), (8,4,4), (2,8,2) }.
- Report: meshes/s, edits/s, queries/s; **correctness:** no crash, no assertion,
  and every produced `ChunkMesh` passes a cheap sanity check (index range valid,
  vertex count = 4 × quad count). Run once more under Linux ASan and TSan
  (BD7 CI job) — expect ASan clean; TSan will flag the documented seqlock benign
  race unless `CELLULOSE_STRICT_ATOMICS` is on (that's the point of B2 → D3).
- **Informs:** the headline "does the whole thing hold up under realistic
  concurrent load" answer.

---

## Reporting format

`docs/benchmarks/RESULTS-<date>-<machine>.md`:

```
## Machine
CPU / cores / RAM / OS / compiler+flags / CMAKE_BUILD_TYPE=Release

## B1 — seqlock hot: readers vs writers
| readers | writers | reads/s | writes/s | write p99 (ns) | retry % | violations |
|---------|---------|---------|----------|----------------|---------|------------|
| 4       | 0       | …       | —        | —              | 0.0     | 0          |
| 4       | 1       | …       | …        | …              | …       | 0          |
...

## Takeaways
- D3: strict-atomics costs X% on the read path → <decision>
- D4: writes/s at 4 writers is Y% of 1 writer → <decision>
- D5: lookups/s plateaus at Z query threads → <decision>
```

---

## Tasks

- [ ] **T1** — `benchmarks/` scaffold: `harness.hpp` (roles, `run`, histogram,
      `DoNotOptimize`, Release guard), `main.cpp` CLI, CMake option +
      `cellulose_benchmarks` target. A trivial `bench_noop` scenario proves the
      harness (spawn/join/count/report). Commit.
- [ ] **T2** — `CELLULOSE_SEQLOCK_STATS` (retry counter) + `CELLULOSE_STRICT_ATOMICS`
      (the D3 `atomic_ref` `read_hot`/`write_hot` path). Both default OFF, both
      compile-tested in the normal suite. Commit.
- [ ] **T3** — B1 + B2 (`bench_seqlock.cpp`). Commit.
- [ ] **T4** — B3 (`bench_rwlock.cpp`). Commit.
- [ ] **T5** — B4 + B5 (`bench_world.cpp`). Commit.
- [ ] **T6** — B6 (`bench_cursor.cpp`). Commit.
- [ ] **T7** — B7 (`bench_workload.cpp`) + the `workflow_dispatch` CI job that
      builds Release and runs `--scenario workload` under plain / ASan / TSan on
      Linux. Commit.
- [ ] **T8** — run everything on the dev machine, write
      `docs/benchmarks/RESULTS-*.md`, and record the D3 / D4 / D5 verdicts in
      `REMAINING_TASKS.md` + `docs/plans/design-followups.md`. Commit.

---

## Verification

1. `cmake -B build -DCELLULOSE_BUILD_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release` →
   `cellulose_benchmarks` builds; the normal `ctest` suite is unaffected with the
   option OFF.
2. `cellulose_benchmarks --scenario noop` runs and reports.
3. Every scenario runs to completion with **zero invariant violations** at every
   thread config (correctness is the gate; throughput is data).
4. `--csv` output parses; a results file is produced.
5. Linux CI `workflow_dispatch` job: benchmarks build + run; ASan clean; TSan
   report is either clean (`STRICT_ATOMICS`) or exactly the known seqlock race.

## Deferred

- Auto-tuned iteration counts / proper confidence intervals (would justify pulling
  in Google Benchmark).
- Latency under artificial scheduler pressure (`nice`, cgroup CPU caps).
- NUMA-aware placement measurements.
- A perf-regression tracker (store results over time, alert on regressions).
