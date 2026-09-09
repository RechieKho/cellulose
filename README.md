# Cellulose: A Voxel Library

`cellulose` is a performant C++ voxel library providing spatial data structures and operations for Minecraft-style voxel engines under concurrent settings.

## Design and Implementation Strategy

The four core design concerns are:

1. Core Data Structure
2. Concurrency and Thread Safety
3. Spatial Querying
4. Rendering Pipeline

### Core Data Structure

World spatial data is organized in a two-tiered hierarchy: world chunks are indexed in a Robin Hood hash table, while voxel data within each chunk is stored in Morton-coded 1D arrays.

* **Robin Hood Hash Table:** Chosen for fast insertions and lookups, as well as high cache locality due to its contiguous memory layout (e.g., [`unordered_dense` by martinus](https://github.com/martinus/unordered_dense)).
* **Morton-Coded 1D Array:** Preserves spatial locality inside each chunk (e.g., [`libmorton` by Forceflow](https://github.com/Forceflow/libmorton)).

#### Data Layout (AoS, SoA, and Sparse Lists)

Chunk data is categorized by access frequency to maximize 64-byte L1 cache line utilization and avoid cache pollution:

1. **Hot Data (< 4 bytes, e.g., meshing flags):** Frequently accessed frame-by-frame. Stored in an **Array of Structures (AoS)** layout using a Morton-coded 1D array. Keeping elements small (< 4 bytes) ensures maximum element density per cache line.
2. **Cold Data (> 8 bytes, e.g., fluid properties):** Frequently accessed, but not every frame. Stored in separate **Structure of Arrays (SoA)** Morton-coded 1D arrays to avoid polluting the cache with unused fields.
3. **Freezing Cold Data (very large, e.g., complex tile entities):** Rarely accessed. Stored per chunk in a **sparse list** to minimize memory overhead.

> **Key Rationale:** Using a single world-level hash table eliminates repeated hash computation overhead. Combining AoS for hot data and SoA for cold data maximizes L1 cache hits while keeping heavy, rarely used data off the main memory path.

### Concurrency and Thread Safety

Concurrency is managed at the **chunk level** to balance memory footprint against thread synchronization overhead (idle waiting).

* **World-Level vs. Block-Level Granularity:** Locking the entire world causes severe thread contention. Conversely, per-block locking eliminates lock contention but requires padding around each block to prevent false sharing, incurring unacceptable memory overhead. Chunk-level locking provides the ideal balance.

#### Resolving Data Races

Voxel workloads are heavily read-dominated (meshing, physics, raycasting). To avoid mutex bottlenecks, `cellulose` uses lightweight lock primitives:

* **Sequence Locks (Hot & Cold Data):** Eliminate write starvation and reduce locking overhead by allowing optimistic, unblocked reads. Because hot and cold arrays have fixed sizes, concurrent updates will not cause out-of-bounds memory errors.
* **Read-Write Locks (Freezing Cold Data):** Protect sparse lists. Since freezing cold data is accessed infrequently, write starvation risks remain minimal.

#### Eliminating False Sharing

To prevent cache invalidation when adjacent memory locations are modified by different threads:

* Chunk structures are aligned to hardware cache boundaries using `alignas` and `std::hardware_destructive_interference_size`.
* Internal chunk locks are padded to isolate lock state synchronization from adjacent voxel data.

### Spatial Querying

`cellulose` provides three core spatial query types:

1. **Raycasting:** Implements an optimized 3D Digital Differential Analyzer (DDA) algorithm based on the [Amanatides & Woo Fast Voxel Traversal Algorithm](https://www.researchgate.net/publication/2611491_A_Fast_Voxel_Traversal_Algorithm_for_Ray_Tracing).
2. **Volumetric Queries:** Provides retrieval functions for voxel data within Axis-Aligned Bounding Boxes (AABBs) and spherical regions.
3. **Collision Detection:** Calculates collision normals and positional corrections for target AABBs and velocity vectors, establishing a baseline for custom physics integration.

### Rendering Pipeline

The rendering pipeline generates optimized mesh geometry from spatial data using **greedy meshing**. To extend effective render distance efficiently, it constructs multiple **Levels of Detail (LOD)** by downsampling voxel clusters into macro-blocks.

## Implementation Status

The foundation — `Chunk` (Morton-coded per-cell storage) and `World` (chunk hash table) —
is implemented and covered by the `cellulose_tests` suite (`ctest --test-dir build`).
Concrete parameters chosen ahead of a full spec (chunk edge length 32, `u32` Morton
index, `i64` world coordinates, `doctest` for tests) are recorded in
[`ARCHITECTURE_SPEC.md`](ARCHITECTURE_SPEC.md). Outstanding work across all four
subsystems is tracked in [`REMAINING_TASKS.md`](REMAINING_TASKS.md); the vendored
design spec and foundation plan live under [`docs/superpowers/`](docs/superpowers).