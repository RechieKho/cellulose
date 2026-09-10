# Cellulose: A Voxel Library

`cellulose` is a performant C++ voxel library providing spatial data structures and operations for Minecraft-style voxel engines under concurrent settings.

## Example — a minimal voxel game

The core of a Minecraft-style edit loop is three calls: `raycast` to find the
targeted block, a `hot_attribute` write to change it, and `mesh_chunk` to rebuild
the render mesh. `src/main.cpp` is the full playable version (raylib window, fly
camera, per-face textures, a custom cold-tier attribute for mining); this is the
substance:

```cpp
#include <cellulose/cellulose.hpp>

cellulose::World<> world;

// generate a slab of terrain in one chunk (1 = grass, 2 = dirt)
for (int x = 0; x < 32; ++x)
    for (int z = 0; z < 32; ++z)
        for (int y = 0; y <= 6; ++y)
            world.chunk({ 0, 0, 0 })
                 .hot_attribute({ cellulose::u8(x), cellulose::u8(y), cellulose::u8(z) })
                 .block_id = (y == 6 ? 1 : 2);

// "solid" for queries and meshing: any non-air block
const auto solid = [](const cellulose::HotCellAttribute &a) { return a.block_id != 0; };

// aim a ray (from a camera, say) and act on what it hits
const cellulose::Ray ray{ /*origin*/ { 5, 20, 5 }, /*direction*/ { 0, -1, 0 } };

if (auto hit = cellulose::raycast(world, ray, 32.0, solid)) {
    // LEFT CLICK — break the block you're looking at
    world.find_chunk(cellulose::to_chunk_position(hit->cell))
         ->hot_attribute(cellulose::to_local_position(hit->cell)).block_id = 0;

    // RIGHT CLICK — place a block against the face you hit
    const cellulose::WorldPosition against{
        hit->cell.x + hit->normal.x, hit->cell.y + hit->normal.y, hit->cell.z + hit->normal.z
    };
    world.chunk(cellulose::to_chunk_position(against))
         .hot_attribute(cellulose::to_local_position(against)).block_id = 1;
}

// rebuild the chunk's geometry after the edit
const cellulose::ChunkMesh mesh = cellulose::mesh_chunk(world, { 0, 0, 0 }, solid);
// mesh.vertices / mesh.indices → your renderer
// (cellulose/raylib.hpp has `to_raylib_mesh` if you use raylib)
```

Every accessor here has a lock-taking counterpart (`read_hot` / `write_hot`,
`ChunkStorage::Shared`, …) for doing this from worker threads — see
[`ARCHITECTURE_SPEC.md`](ARCHITECTURE_SPEC.md).

### Textures

Give each block a texture per face and mesh against it — the resolved texture id
joins the greedy merge key, so faces only merge within one texture:

```cpp
const auto registry = cellulose::BlockRegistryBuilder()
    .add_block_builder(cellulose::BlockBuilder("air"))
    .add_block_builder(cellulose::BlockBuilder("grass")
        .texture_column(/*top*/ 1, /*side*/ 2, /*bottom*/ 3)) // Minecraft-style
    .add_block_builder(cellulose::BlockBuilder("dirt").texture_all(3))
    .build();

// `registry` is itself the resolver — pass it to the mesher; MeshOptions is optional
cellulose::ChunkMesh mesh = cellulose::mesh_chunk(
    world, { 0, 0, 0 }, solid, registry, { .ambient_occlusion = true });
// each vertex now carries `texture_id` and `occlusion`

// lay the tiles out in a near-square power-of-two grid; blit your pixels into
// atlas.rect_of(id) and upload one texture
const std::array<cellulose::TextureID, 3> ids{ 1, 2, 3 };
const cellulose::PackedAtlas sheet = cellulose::pack_grid(ids, /*tile_px*/ 16);
const cellulose::TextureAtlas &atlas = sheet.atlas;
```

`cellulose/raylib.hpp` turns that into a drawable: `to_raylib_mesh(mesh, atlas)`
plus `load_atlas_shader(atlas)` / `load_atlas_material(...)` — one 2‑D texture,
one draw call, and a tiling shader that keeps greedy-merged quads correct.

### Custom cell attributes

The **hot** tier ships (`HotCellAttribute` — block id + per-face state). The
**cold** and **freezing** tiers are yours: pick the types, and each chunk stores
one dense SoA array (cold, elements ≤ 8 bytes) or a sparse per-cell map
(freezing) for them. Add a byte of mining progress:

```cpp
struct Damage { cellulose::u8 hits = 0; };  // your own attribute type

using GameChunk = cellulose::Chunk<
    cellulose::HotCellAttribute,
    cellulose::PackedChunkAttributes<Damage>>;   // cold tier = { Damage }
cellulose::World<GameChunk> world;

const auto index = cellulose::encode_cell_index(cellulose::to_local_position(cell));
auto *chunk = world.find_chunk(cellulose::to_chunk_position(cell));

// read + write it under the cold-tier lock (functor accessors run your closure)
cellulose::u8 hits = chunk->read_cold([&](const auto &cold) {
    return cold.template get<Damage>()[index].hits;
});
chunk->write_cold([&](auto &cold) { cold.template get<Damage>()[index].hits = hits + 1; });
```

`src/main.cpp` uses exactly this for its hold-to-mine mechanic.

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

**Texturing** is consumer-driven: blocks carry a texture id per face (`BlockRegistry`), the mesher folds the resolved id into the merge key and onto every vertex, and a renderer-neutral atlas layout (`TextureAtlas` / `atlas_builder`) maps ids to sheet rects. The opt-in raylib bridge draws it from a single 2‑D atlas with an `origin + fract(uv) * tileSize` tiling shader — no texture-array feature, no platform-specific GL.

**Ambient occlusion** is an opt-in mesher flag (`MeshOptions{ .ambient_occlusion = true }`): 0fps-style per-corner AO, folded into the greedy merge key so open surfaces still merge and only faces touching an edge or crevice split into their own quads. Each `MeshVertex` carries an `occlusion` factor (`1.0` when the flag is off — geometry is then byte-identical).

**T-junction welding** (`MeshOptions{ .weld_t_junctions = true }`) stitches the cracks greedy meshing leaves where a wide quad abuts narrower ones, so the sky doesn't show through as flickering pixels along size steps.

## Implementation Status

All four subsystems above are implemented and covered by the `cellulose_tests`
suite (`ctest --test-dir build`). The as-built design and every locked decision
are in [`ARCHITECTURE_SPEC.md`](ARCHITECTURE_SPEC.md); outstanding work in
[`REMAINING_TASKS.md`](REMAINING_TASKS.md); per-phase plans under
[`docs/plans/`](docs/plans). **Contributors / agents: start with
[`STATE.md`](STATE.md)** — build/test commands and the recurring gotchas.
