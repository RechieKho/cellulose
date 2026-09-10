# Texture Management — Plan & Decisions

A rendering-side subsystem: let the consumer assign a texture to every block
(per face), and let the mesher + bridge turn that into atlas-mapped geometry.
Currently there is nothing — `MeshVertex` carries `block_id` + tile-space `u/v`
and the consumer resolves colour/texture itself (the demo `switch`es on
`block_id`).

## Decisions (locked 2026-09-10)

| # | Question | Decision |
|---|----------|----------|
| 1 | Atlas sampling strategy | **C1 — one CPU-built 2-D atlas + a tiny tiling shader.** ~~C2 texture array~~ was pivoted away from: raylib exposes no `GL_TEXTURE_2D_ARRAY` API, so it forced ~40 lines of raw-GL / `wglGetProcAddress` glue into the bridge that would multiply per new target platform. C1 is stock raylib — one `Texture2D`, one draw call, greedy merging preserved; the shader does `origin + fract(tileUV) * tileSize`. |
| 2 | Where texture assignments live | **Extend `Block` / `BlockRegistry`.** `Block` gains `FaceTextures`; `BlockBuilder` gains `.texture*()`; `BlockRegistry` exposes a `face_texture(block, face)` resolver the mesher accepts. |
| 3 | Rect packer | **Include now** — `atlas_builder.hpp`, renderer-neutral, size-only. Produces the `TextureAtlas` the bridge samples and lays out the sheet image the consumer blits into. |
| 4 | `MeshVertex` | Add a `TextureID texture_id` field (40 → 44 bytes). |
| 5 | LOD meshing | Thread `texture_id` through `mesh_chunk_lod` too. |

`texture_id` is renderer-agnostic: the mesher only folds it into the merge key
and copies it onto each vertex. The **bridge** turns it into an atlas rect via
`TextureAtlas::rect_of(texture_id)` — so a merged N×M quad carries tile-space
UVs `[0,N]×[0,M]` plus its tile's atlas origin, and the shader tiles within the
tile. A uniform-grid atlas means `tileSize` is one shader uniform, not per-vertex.

## Principles (unchanged from the rest of the library)

1. **Renderer-neutral core.** `mesh.hpp` / a new `texture.hpp` know nothing about
   GPUs or image files — no `stb_image`, no `Texture2D`.
2. **Consumer supplies the concrete data** — texture ids, the atlas layout, and
   the actual pixels. The library manages the *mapping* and the *UV math*.
3. **Opt-in bridge does the GPU glue.** `raylib.hpp` grows the upload + shader.
4. **Back-compat.** `Chunk<>` and `mesh_chunk(world, cp, is_solid)` are unchanged;
   an untextured build still gets today's flat-colour path.

---

## A. Texture assignment — `inc/cellulose/block.hpp`

`TextureID` / `FaceTextures` sit next to `BlockID` (small value types, no
rendering deps); the assignment lives on `Block`, filled through `BlockBuilder`.

```cpp
namespace cellulose {

using TextureID = u32;                 // consumer-defined; also the array layer. 0 == default

/// Per-face texture ids. Face order is the library's canonical order,
/// matching `face_brightness` and every query:  0 +X  1 -X  2 +Y  3 -Y  4 +Z  5 -Z
struct FaceTextures final {
    std::array<TextureID, 6> faces{};

    static constexpr auto uniform(TextureID) -> FaceTextures;              // all six
    static constexpr auto column(TextureID top, TextureID side,
                                 TextureID bottom) -> FaceTextures;        // grass-style
    constexpr auto operator[](i32 face) const -> TextureID;
};

// impl::Block<> gains:      FaceTextures textures{};
// impl::BlockBuilder<> gains: FaceTextures textures{};
//   auto texture(FaceTextures) -> BlockBuilder &;
//   auto texture_all(TextureID) -> BlockBuilder &;                        // FaceTextures::uniform
//   auto texture_column(TextureID top, TextureID side, TextureID bottom) -> BlockBuilder &;

// impl::BlockRegistry<> gains:
//   auto face_texture(BlockID, i32 face) const -> TextureID;             // 0 if id out of range / unset
//   /// Usable directly as the mesher's `texture_of` resolver.
//   template <HotAttribute H>
//   auto operator()(const H &p_attr, i32 p_face) const -> TextureID
//       { return face_texture(p_attr.block_id, p_face); }

} // namespace cellulose
```

`Block` grows by 24 bytes (`std::array<u32,6>`); it is a cold definition type,
not on any hot path. `BlockRegistry` already exists to "let the renderer reach a
block by id from `HotCellAttribute`" (its own doc comment) — this is exactly that.

## A2. Atlas geometry — `inc/cellulose/texture.hpp` (renderer-neutral, umbrella)

Maps a `TextureID` to its rect in the sheet. The bridge samples it; the mesher
never touches it. (`layers(count)` mode is a degenerate whole-sheet map, kept for
symmetry / non-atlas consumers.)

```cpp
namespace cellulose {

/// Normalised UV rectangle, origin top-left, y-down (raylib convention).
struct UvRect final { Vec2 min; Vec2 max; };

/// TextureID -> placement. Two modes:
///   * layers(count)      — array mode: `rect_of` is the full [0,1]², `layer_of(id) == id`
///   * grid(cols, rows)   — 2-D sheet: tile `id` at (id % cols, id / cols)
///   * set_rect(id, rect) — 2-D sheet: explicit (e.g. from the packer)
class TextureAtlas final {
public:
    static auto layers(u32 count) -> TextureAtlas;
    static auto grid(u32 columns, u32 rows) -> TextureAtlas;
    auto set_rect(TextureID, UvRect) -> TextureAtlas &;
    auto rect_of(TextureID) const -> UvRect;
    auto tile_size() const -> Vec2;
};

} // namespace cellulose
```

---

## B. Mesher integration — `mesh.hpp`, `cell.hpp`

1. `MeshVertex` gains `TextureID texture_id;` (kept **alongside** `block_id` —
   `block_id` stays the semantic "what block", `texture_id` is the render key).
2. `MeshSample` gains `std::array<TextureID, 6> texture{};`.
3. New customization point, mirroring `face_brightness`:

   ```cpp
   // cell.hpp — default: texture id == block id (uniform per block, back-compat)
   constexpr auto face_texture(const HotCellAttribute &, i32) -> TextureID; // == block_id

   // mesh.hpp — impl::sample_face_texture(attr, face):
   //   if constexpr (requires { face_texture(attr, face); }) face_texture(...)
   //   else static_cast<TextureID>(attr.block_id)
   ```

4. New overloads that take an explicit resolver `texture_of(attr, face) -> TextureID`
   (`BlockRegistry` satisfies it directly — pass the registry):

   ```cpp
   mesh_chunk(world, cp, is_solid, texture_of)
   mesh_chunk(world, cp, has_geometry, is_hidden, texture_of)
   mesh_chunk_lod(world, cp, level, is_solid, texture_of)      // + rule-split form
   ```

   The existing arities are untouched; they route through `sample_face_texture`
   (→ `block_id`), so vertex counts and output are **identical to today**.

5. **Merge key.** Today: `u32 = (block_id << 8) | brightness`, `+1` sentinel.
   New: `u64 = ((u64)texture_id << 2) | brightness`, `+1`. Consequences:
   - faces merge iff **same texture id (array layer) and same brightness** — a
     merged quad is one layer, sampled with `REPEAT`, so it tiles for free;
   - two different block ids that share a face texture now merge (a small win);
   - `block_id` on the emitted vertices is taken from the run's first cell, the
     same way brightness and attributes already are.

---

## C. Sampling — one 2-D atlas + tiling shader (decision 1 = C1)

Greedy meshing merges an `N×M` run into one quad with `u ∈ [0,N], v ∈ [0,M]`.
Plain `GL_REPEAT` on an atlas wraps the *whole sheet* and bleeds between tiles,
so the shader tiles within the tile instead:

```glsl
vec2 uv = tileOrigin + fract(vTileUV) * uTileSize;
finalColor = texture(atlas, uv) * vec4(vec3(vBrightness), 1.0);
```

* `vTileUV` — the mesh's existing tile-space `texcoords` (`[0,N]×[0,M]`).
* `tileOrigin` — `TextureAtlas::rect_of(texture_id).min`, baked per-vertex into
  `texcoords2` by the bridge (raylib's spare `Vector2` stream).
* `uTileSize` — one uniform `vec2 (1/columns, 1/rows)` (uniform-grid atlas).
* Wrap `CLAMP`, filter `NEAREST` — the voxel look, and `NEAREST` sidesteps the
  mip / linear-filter seam that `fract` would otherwise introduce.

Because `texture_id` is in the merge key (§B.5) a merged quad is always one
tile, so one `tileOrigin` per quad is exact. A consumer who wants linear
filtering / mipmaps adds a half-texel inset to `uTileSize` and `textureGrad` —
documented, not the default.

---

## D. `raylib.hpp` bridge additions

```cpp
/// ChunkMesh -> Mesh. `p_atlas.rect_of(vertex.texture_id).min` is baked into
/// texcoords2; texcoords keep the tile-space [0,N] UVs; brightness -> vertex colour.
auto to_raylib_mesh(const ChunkMesh &, const TextureAtlas &) -> Mesh;

/// GLSL 330 vertex + fragment sources for the tiling sampler above.
inline constexpr const char *atlas_tiling_vs;
inline constexpr const char *atlas_tiling_fs;

/// Compile that shader and set `uTileSize` from the atlas grid. `mvp` /
/// `matModel` are auto-wired by raylib.
auto load_atlas_shader(const TextureAtlas &) -> Shader;

/// LoadMaterialDefault + the shader + the atlas Texture2D as MAP_DIFFUSE.
auto load_atlas_material(Shader, Texture2D atlas) -> Material;
```

All stock raylib — `LoadShaderFromMemory`, `LoadTextureFromImage`, a normal
`Material` / `DrawMesh`. No `rlgl`, no raw GL, nothing platform-specific. The
consumer builds the atlas `Image` (see §E) and calls `LoadTextureFromImage`.

The existing flat-colour path (`to_raylib_mesh(mesh, color_fn)`) is untouched
for consumers not using textures.

---

## E. `atlas_builder.hpp` — size-only rect packer (decision 3 = include now)

Renderer-neutral, no image decode. `pack(std::span<const TileSize>) -> PackedAtlas`
where `TileSize{ TextureID id; u32 w, h; }` and `PackedAtlas` holds a
`TextureAtlas` + sheet `w/h`. Shelf packer (~90 LOC). The consumer allocates a
`w × h` image, blits each tile's pixels into `rect_of(id)`, uploads it once.

`pack_grid(ids, tile_px)` is the uniform-tile convenience — a **near-square
power-of-two** `TextureAtlas` (`Grid` mode) plus the sheet size, id `n` at cell
`(n % columns, n / columns)`. (Originally shipped as a `1 × N` `strip`, replaced
2026-09-10: GPUs sample and cache a square sheet better and a strip hits
`GL_MAX_TEXTURE_SIZE`.) The demo builds this layout procedurally.

The library never sees a pixel.

---

## F. Demo

Blocks get textures through `BlockBuilder`: grass =
`.texture_column(grass_top, grass_side, dirt)`, dirt / stone = `.texture_all(...)`.
The demo builds a tiny procedural atlas `Image` in code (no checked-in asset)
from `pack_grid(ids, 16)`, `LoadTextureFromImage`, `load_atlas_shader` +
`load_atlas_material`, meshes with the `BlockRegistry` resolver,
`to_raylib_mesh(mesh, atlas)`. README example gains a few lines showing
`BlockBuilder::texture_column` + the textured `mesh_chunk` overload.

---

## Tests

- `FaceTextures::uniform` / `column` map the six faces correctly.
- `BlockBuilder::texture*` → `BlockRegistry::face_texture` returns set values,
  `0` for unset / out-of-range ids; `BlockRegistry` works as a mesher resolver.
- `TextureAtlas::grid` rect math (tile 5 of a 4×4 → row 1 col 1; corners
  correct); explicit `set_rect` overrides, unset id reads whole-sheet.
- `atlas_builder::pack` — no overlaps, everything inside the sheet;
  `pack_grid(ids, px)` → near-square power-of-two grid, id `n` at `(n%cols, n/cols)`.
- Mesher: a `column` grass block emits distinct `texture_id` on top / side /
  bottom; a run of identical-texture faces still merges to one quad; two block
  ids sharing a face texture merge.
- Back-compat: `mesh_chunk(world, cp, is_solid)` → `texture_id == block_id` and
  byte-identical geometry to the pre-change mesher on a fixed scene.

---

## Phasing

| Step | Content | Status |
|---|---|---|
| **TX1** | `block.hpp`: `TextureID`, `FaceTextures`, `Block::textures`, `BlockBuilder::texture*()`, `BlockRegistry::face_texture` + resolver. `texture.hpp`: `UvRect`, `TextureAtlas`. Umbrella. Tests. | ✅ `f178b59` |
| **TX2** | `atlas_builder.hpp`: `TileSize`, `PackedAtlas`, `pack`, `pack_grid`. Umbrella. Tests. | ✅ `f178b59` |
| **TX3** | mesher: `MeshVertex::texture_id`, `MeshSample::texture`, `face_texture` CPO + `impl::sample_face_texture`, resolver overloads (incl. LOD), merge key folds in texture. Tests. | ✅ `f178b59` |
| **TX4** | `raylib.hpp`: `to_raylib_mesh(mesh, atlas)`, `atlas_tiling_vs` / `_fs`, `load_atlas_shader`, `load_atlas_material`. | |
| **TX5** | demo: procedural atlas + per-face grass via `BlockBuilder`; README note. | |
| **TX6** | docs: `ARCHITECTURE_SPEC.md` §4 + decision table, close the `REMAINING_TASKS` texture-atlas item, `STATE.md` gotchas (canonical face order, `texcoords2` = tile origin, `fract` tiling shader, `NEAREST`/`CLAMP`). | |

---

## Verification

1. Warning-free build of `inc/cellulose/*`; `cellulose` + `cellulose_tests` link.
2. `ctest` — all prior cases pass unchanged; new cases green.
3. `Chunk<>` and every existing `mesh_chunk` / `mesh_chunk_lod` call site compile
   and produce identical geometry (fixed-scene byte compare).
4. `clang-format` clean; demo runs, textured terrain renders, no GL errors.
5. CI (Linux/macOS/Windows build) green — the bridge is stock raylib, no
   platform-specific code.
