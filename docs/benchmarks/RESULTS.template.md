# Concurrency benchmark results — <yyyy-mm-dd> — <machine>

Copy this file to `RESULTS-<yyyy-mm-dd>-<machine>.md` and fill it in from a run of
`cellulose_benchmarks` built **Release**. See `docs/plans/concurrency-benchmarks.md`.

## Machine

- CPU / cores (physical + logical):
- RAM:
- OS:
- Compiler + flags: (`CMAKE_BUILD_TYPE=Release`, …)
- Command: `cellulose_benchmarks --scenario all --duration-ms 3000 --runs 5 --csv`
- Notes: (turbo / thermal / other load)

## B1 — seqlock hot tier (default build)

| readers | writers | reads/s | writes/s | wr-p50 | wr-p99 | retry rate | violations |
|---------|---------|---------|----------|--------|--------|------------|------------|
|         |         |         |          |        |        |            | 0          |

## B2 — seqlock hot tier: default vs `CELLULOSE_STRICT_ATOMICS`

| config | default reads/s | strict reads/s | Δ | default writes/s | strict writes/s |
|--------|-----------------|----------------|---|------------------|-----------------|
| 4r / 0w (pure read) | | | | — | — |
| 4r / 1w | | | | | |

## B3 — sparse RWLock

| readers | writers | reads/s | writes/s | rd-p99 | rd-max | wr-p99 |
|---------|---------|---------|----------|--------|--------|--------|

## B4 — world directory: lookup vs churn

| chunks | queriers | streamers | lookups/s | dir-writes/s | lu-p99 | lu-max |
|--------|----------|-----------|-----------|--------------|--------|--------|

## B5 — `ChunkStorage::Shared` vs `Unique`

| queriers | unique lookups/s | shared lookups/s | Δ |
|----------|------------------|------------------|---|

## B6 — ChunkCursor

| variant | walkers | cells/s | errors |
|---------|---------|---------|--------|

## B7 — mixed workload

| meshers | editors | queriers | meshes/s | edits/s | queries/s | mesh errors |
|---------|---------|----------|----------|---------|-----------|-------------|
|         |         |          |          |         |           | 0           |

Sanitizers (Linux CI `Benchmarks` workflow): TSan `<clean / findings>`, ASan `<clean / findings>`.

## Verdicts

- **D3** (seqlock `atomic_ref`): strict-atomics costs **__%** on the pure-read
  fast path → `<make it default / keep opt-in>`.
- **D4** (CAS single-writer): writes/s at N writers is **__%** of 1 writer →
  `<worth it / not yet>`.
- **D5** (shard the directory): lookups/s plateaus at **__** query threads;
  churn drops it to **__%** → `<shard / leave>`.
- **`ChunkStorage::Shared` cost**: **__%** on lookups → `<acceptable / design a lighter handle>`.
