# Design Follow-ups — Decisions & Plan

The four README subsystems are built. This records the resolution of the ten open
design questions raised while reviewing the backlog, and the plan for the ones
being acted on now.

**Guiding principle (from the library author):** the base voxel library provides
the *machinery* — tiered per-cell storage, Morton indexing, chunk-level locks,
spatial queries, the greedy mesher — and the *consumer* supplies every concrete
attribute type. The base ships **no** concrete cold/freezing attributes because
it has no need for them; the same restraint applies to concurrency machinery
(no epoch reclamation, no sharded directory) until a real workload demands it.

---

## Decisions

| # | Question | Decision | Now / later |
|---|----------|----------|-------------|
| 1 | Attribute extensibility | Template `Chunk` on the **hot** type too (`Chunk<HotType = HotCellAttribute, Packed, Sparse>`); a `HotAttribute` concept requires only `block_id`; the mesher reads brightness through a `face_brightness` customization point (default for `HotCellAttribute`, flat otherwise). Add `PackedChunkAttributes` / `SparseChunkAttributes` aliases so adding a tier attribute doesn't repeat `chunk_cell_count`. | **now** |
| 2 | Thread-safe chunk unload | Keep the caller-quiescence contract as the default. Add an opt-in `World<ChunkType, ChunkStorage::Shared>` policy that stores `shared_ptr<Chunk>` — `find_chunk` then returns a handle that keeps the chunk alive. No epoch/hazard machinery. | **now** |
| 3 | Seqlock memory model | Add the strict `std::atomic_ref` element path (`CELLULOSE_STRICT_ATOMICS`). **Benchmark verdict (B2 + Linux TSan): adopt as the default** — ≈free on the meshing read path, makes the layer race-free. Breaking (whole-element `write_hot` closures); land deliberately. | **done** — default on; opt out with `CELLULOSE_LOOSE_ATOMICS`; torn-read tests are loose-only |
| 4 | CAS single-writer seqlock | Keep the per-tier `std::mutex`. **Benchmark verdict (B1/B7): still defer** — writer contention is modest (−30% at 4 writers). The ~160 B/chunk saving is the real motive; revisit on a world-gen profile. | later |
| 5 | World directory scaling | **Benchmark verdict (B4): adopt — shard `m_chunks`.** Pure lookups don't scale past ~1 useful thread and one concurrent streamer cuts throughput ~5× (four ~14×) with tens-of-ms tails. N independent `{shared_mutex, sub-map}` by hash bits. | **done** — `world.hpp`: 16 hash-bit shards, each a padded `{shared_mutex, map}`; `for_each_chunk` / `chunk_count` lock all in index order |
| 6 | Ambient occlusion | Consumer's concern. If built into the mesher, it is an **opt-in** flag with AO folded into the merge key (accepting fewer merges), never default. | later (opt-in) |
| 7 | Non-cube block shapes | Out of scope. Greedy meshing is a cube optimisation; a block-model system belongs in the consumer's engine. The library gives `mesh_chunk` for the cube bulk + `for_each_cell_in_*` to locate special cells. | won't do |
| 7b | Bitwise / SIMD greedy meshing | Won't do. ~30× faster but needs a linear column-major layout so 64 cells pack into a `u64`; the hot array is Morton-ordered for spatial-query / physics locality (the primary use). Not concurrency-limited — the mesher is already lock-free on a thread-local buffer. Scalar mesher is the deliberate balance. | won't do |
| 8 | Transparency / cutout | Generalise the mesher: `has_geometry(attr)` decides whether a cell emits faces, `is_hidden(near, far)` decides face culling. `mesh_chunk(world, cp, is_solid)` stays as the opaque-cube convenience; a 4-arg overload takes the two rules. | **now** |
| 9 | Incremental remesh | Add an atomic `Chunk::revision` counter bumped by every `write_*` — a general "has this chunk changed" signal (meshing, networking, saving). A `ChunkMeshCache` helper (dirty set + neighbour invalidation) later, as an optional module. | **now** (counter) |
| 10 | Continuous collision / casts | Keep `move_aabb` (axis-separated, tunnel-free per axis). Add `sweep_aabb` (Minkowski time-of-impact, no resolution) and sphere/capsule casts when a use appears. | later (on demand) |

---

## Plan for the "now" items

### T1 — Hot-tier extensibility (`#1`)

**Files:** `cell.hpp`, `chunk.hpp`, `world.hpp`, `mesh.hpp`, `raycast.hpp`,
`collision.hpp`, `volume.hpp`, `cursor.hpp`; tests.

- [ ] `cell.hpp`: `concept HotAttribute = std::is_trivially_copyable_v<T> &&
      std::default_initializable<T> && requires(T a) { { a.block_id } -> std::convertible_to<BlockID>; };`
      Keep `HotCellAttribute`'s own `sizeof <= 4` assert.
- [ ] `cell.hpp`: default `face_brightness(const HotCellAttribute &, i32 face) -> u8`
      free function (the six getters); this is the mesher's customization point.
- [ ] `chunk.hpp`: `impl::Chunk<HotAttribute HotType = HotCellAttribute,
      PackedCollection = …, SparseCollection = …>`; `HotStorage = std::array<HotType, chunk_cell_count>`;
      every `HotCellAttribute` in the accessors → `HotType`. Alias
      `cellulose::Chunk<HotType, Packed, Sparse>` to match. Add
      `PackedChunkAttributes<Attrs…>` / `SparseChunkAttributes<Attrs…>` aliases.
- [ ] `world.hpp`: `find_hot_attribute` returns `HotType *` (deduced from `ChunkType`).
- [ ] `mesh.hpp`: `sample_chunk` becomes generic on the world's hot type;
      brightness via `impl::sample_face_brightness(attr, face)` =
      `if constexpr (requires { face_brightness(attr, face); }) … else 3`.
- [ ] Tests: a custom 2-byte hot type (`struct { BlockID block_id; }`) meshes
      (flat-shaded) and raycasts; `HotCellAttribute` path unchanged.
- [ ] Commit.

### T2 — `Chunk::revision` (`#9`)

- [ ] `chunk.hpp`: `Padded<std::atomic<u64>> m_revision;`; each `write_hot` /
      `write_cold` / `write_sparse` does `m_revision->fetch_add(1, release)` after
      the write; `auto revision() const -> u64` (acquire load). Documented as a
      conservative "may have changed" signal (bumps even on a no-op write).
- [ ] Tests: revision is 0 at rest, strictly increases per write, unaffected by reads.
- [ ] Commit.

### T3 — Mesher rule generalisation (`#8`)

- [ ] `mesh.hpp`: `MeshSample` gains `std::array<bool, 6> visible{}`. `sample_chunk`
      computes it per interior cell/face: `has_geometry(self) && !is_hidden(self, neighbour)`
      (absent-chunk neighbour = a default `HotType{}`). `greedy_mesh` keys off
      `sample.visible[face]` instead of `!at(neighbour).solid`.
- [ ] `mesh_chunk(world, cp, is_solid)` — unchanged signature; builds
      `has_geometry = is_solid`, `is_hidden = [&](auto &, auto &far){ return is_solid(far); }`.
- [ ] `mesh_chunk(world, cp, has_geometry, is_hidden)` — new 4-arg overload;
      same for `mesh_chunk_lod`.
- [ ] Tests: a "glass vs stone" scene — glass-stone interface culled, glass-air
      face kept, glass-glass face culled or kept per the rule.
- [ ] Commit.

### T4 — `World` storage policy (`#2`)

- [ ] `world.hpp`: `enum class ChunkStorage { Unique, Shared };`
      `World<ChunkType = Chunk<>, ChunkStorage Storage = ChunkStorage::Unique>`.
      `Unique` → `unique_ptr` value, `find_chunk -> ChunkType*`.
      `Shared` → `shared_ptr` value, `find_chunk -> shared_ptr<ChunkType>` (a copy —
      pins the chunk past `remove_chunk`). `chunk()` / `for_each_chunk` /
      `find_hot_attribute` adapt; `ChunkCursor` stores `decltype(find_chunk(…))`
      so it pins automatically under `Shared`.
- [ ] Tests: under `Shared`, a handle taken before `remove_chunk` still reads the
      chunk; the map no longer lists it.
- [ ] Commit.

### T5 — Docs

- [ ] `ARCHITECTURE_SPEC.md`: §1.4 (hot type parameter + concept), §1.5 (storage
      policy), §1.6 (`revision`), §4 (mesher rules); decision table rows.
- [ ] `REMAINING_TASKS.md`: close #1/#2/#8/#9; record #3–#7/#10 as *decided —
      deferred* with the trigger for each.
- [ ] Full sweep: `ctest` green, `clang-format` clean, ASan links, demo runs.
- [ ] Commit.

---

## Verification

1. Clean build warning-free for `inc/cellulose/*`; `cellulose` + `cellulose_tests` link.
2. `ctest` — all prior cases pass unchanged; new cases green.
3. A downstream `Chunk<MyTinyHot>` (2-byte, `block_id` only) compiles, meshes
   (flat), raycasts, collides.
4. `HotCellAttribute` remains the zero-config default — `Chunk<>` and every
   existing call site unchanged.
5. `clang-format` clean; ASan build links.
