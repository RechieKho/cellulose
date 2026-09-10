# Texture Management — Plan & Decisions

A rendering-side subsystem: let the consumer assign a texture to every block
(per face), and let the mesher + bridge turn that into atlas-mapped geometry.
Currently there is nothing — `MeshVertex` carries `block_id` + tile-space `u/v`
and the consumer resolves colour/texture itself (the demo `switch`es on
`block_id`).

## Decisions (locked 2026-09-10)

| # | Question | Decision |
|---|----------|----------|
| 1 | Atlas sampling strategy | **C2 — texture array** (`GL_TEXTURE_2D_ARRAY`, one layer per texture id). No `fract`, no bleed, `REPEAT` per layer keeps greedy merging free. All tiles are one size. |
| 2 | Where texture assignments live | **Extend `Block` / `BlockRegistry`.** `Block` gains `FaceTextures`; `BlockBuilder` gains `.texture*()`; `BlockRegistry` exposes a `face_texture(block, face)` resolver the mesher accepts. |
| 3 | Rect packer | **Include now** — `atlas_builder.hpp`, renderer-neutral, size-only. Feeds the *array-strip* image layout for C2 (and a 2-D sheet for non-array consumers). |
| 4 | `MeshVertex` | Add a `TextureID texture_id` field (40 → 44 bytes). |
| 5 | LOD meshing | Thread `texture_id` through `mesh_chunk_lod` too. |

Because C2 addresses tiles by **layer index**, `texture_id` *is* the array layer
— there is no per-tile `UvRect` on the hot path. `TextureAtlas` / the packer
still produce rects, but only for (a) laying out the vertical strip image the
array is uploaded from and (b) consumers targeting a plain 2-D sheet on another
renderer. The mesher and the raylib array path never look at a `UvRect`.

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

Only needed to lay out the strip image the array is uploaded from, and for
non-array consumers. The mesher never touches these.

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

## C. Sampling — texture array (decision 1 = C2)

Greedy meshing merges an `N×M` run into one quad with `u ∈ [0,N], v ∈ [0,M]`.
A `GL_TEXTURE_2D_ARRAY` sampled `texture(sampler2DArray, vec3(uv, layer))` with
`REPEAT` wrap tiles that quad correctly with **no `fract`, no bleed** — each
layer is its own image, so a merged run of one layer just repeats. This is why
the merge key folds in `texture_id` (§B.5): a quad is always a single layer.

The layer index reaches the shader as a per-vertex value. raylib's `Mesh` has no
spare integer stream, so the bridge writes it into `texcoords2.x` (a `Vector2`
slot otherwise used for lightmaps) and the shader reads
`int layer = int(fragTexCoord2.x + 0.5);`.

All tiles are one size (`tile_px`, e.g. 16). Non-uniform textures are the
consumer's problem to pre-scale — the same constraint every array-texture engine
has.

---

## D. `raylib.hpp` bridge additions

```cpp
/// ChunkMesh -> Mesh, layer index baked into texcoords2.x. Pair with the shader.
auto to_raylib_mesh_array(const ChunkMesh &) -> Mesh;

/// GLSL 330 vertex+fragment sources for sampler2DArray + per-vertex layer + brightness.
inline constexpr const char *array_vs;
inline constexpr const char *array_fs;

/// Upload `count` layers of `tile_px` from one vertical strip Image
/// (height == count * tile_px). Returns a raw GL array-texture id wrapped in Texture2D.
auto load_texture_array(Image strip, u32 tile_px, u32 count) -> Texture2D;

/// Texture-array + compiled Shader + Material wired to sample it.
auto load_array_material(Texture2D array_texture) -> Material;
```

Implemented with `rlgl` (`rlLoadTexture` won't do array textures — use
`glTexImage3D` via `rlLoadTextureDepth`-style raw calls, guarded to GL ≥ 3.3).
The consumer still calls `LoadImage` on their own PNG strip — image IO stays out.

The existing flat-colour path (`to_raylib_mesh` + a `block_colour` fn) is
untouched for consumers not using textures.

---

## E. `atlas_builder.hpp` — size-only rect packer (decision 3 = include now)

Renderer-neutral, no image decode. `pack(std::span<const TileSize>) -> PackedAtlas`
where `TileSize{ TextureID id; u32 w, h; }` and `PackedAtlas` holds a
`TextureAtlas` (rect mode) + total sheet `w/h`. Shelf/skyline packer (~90 LOC).

Two uses:
- **Array strip layout**: with all tiles `tile_px` square, `pack` degenerates to
  a 1-column strip — `count * tile_px` tall — which `load_texture_array` consumes
  directly. A `strip(ids, tile_px)` convenience wraps this.
- **2-D sheet** for consumers on another renderer / a 2-D atlas path — the
  general non-uniform case.

The consumer blits pixels into the returned rects; the library never sees a
pixel.

---

## F. Demo

Blocks get textures through `BlockBuilder`: grass =
`.texture_column(grass_top, grass_side, dirt)`, dirt / stone = `.texture_all(...)`.
The demo builds a tiny procedural strip `Image` in code (no checked-in asset),
`load_texture_array` + `load_array_material`, meshes with the `BlockRegistry`
resolver, `to_raylib_mesh_array`. README example gains a few lines showing
`BlockBuilder::texture_column` + the textured `mesh_chunk` overload.

---

## Tests

- `FaceTextures::uniform` / `column` map the six faces correctly.
- `BlockBuilder::texture*` → `BlockRegistry::face_texture` returns set values,
  `0` for unset / out-of-range ids; `BlockRegistry` works as a mesher resolver.
- `TextureAtlas::grid` / `layers` rect math (tile 5 of a 4×4 → row 1 col 1; UV
  corners correct; `layers` → full-`[0,1]²` rect, `layer_of(id) == id`).
- `atlas_builder::pack` — no overlaps, everything inside the sheet;
  `strip(ids, px)` → 1-column, `count*px` tall.
- Mesher: a `column` grass block emits distinct `texture_id` on top / side /
  bottom; a run of identical-texture faces still merges to one quad; two block
  ids sharing a face texture merge.
- Back-compat: `mesh_chunk(world, cp, is_solid)` → `texture_id == block_id` and
  byte-identical geometry to the pre-change mesher on a fixed scene.

---

## Phasing

| Step | Content |
|---|---|
| **TX1** | `block.hpp`: `TextureID`, `FaceTextures`, `Block::textures`, `BlockBuilder::texture*()`, `BlockRegistry::face_texture` + resolver `operator()`. `texture.hpp`: `UvRect`, `TextureAtlas` (`layers` / `grid` / explicit). Umbrella. Tests. Commit. |
| **TX2** | `atlas_builder.hpp`: `TileSize`, `PackedAtlas`, `pack`, `strip`. Umbrella. Tests. Commit. |
| **TX3** | mesher: `MeshVertex::texture_id`, `MeshSample::texture`, `face_texture` CPO + `impl::sample_face_texture`, resolver overloads (incl. LOD), `u64` merge key; tests; commit. |
| **TX4** | `raylib.hpp`: `to_raylib_mesh_array`, `array_vs` / `array_fs`, `load_texture_array`, `load_array_material`; commit. |
| **TX5** | demo: procedural strip + per-face grass via `BlockBuilder`; README note; commit. |
| **TX6** | docs: `ARCHITECTURE_SPEC.md` §4 + decision table, close the `REMAINING_TASKS` texture-atlas item, `STATE.md` gotchas (canonical face order, `texcoords2.x` layer channel, `rlgl` array-texture path, GL 3.3 guard); commit. |

---

## Verification

1. Warning-free build of `inc/cellulose/*`; `cellulose` + `cellulose_tests` link.
2. `ctest` — all prior cases pass unchanged; new cases green.
3. `Chunk<>` and every existing `mesh_chunk` / `mesh_chunk_lod` call site compile
   and produce identical geometry (fixed-scene byte compare).
4. `clang-format` clean; demo runs, textured terrain renders, no GL errors.
5. CI (Linux/macOS/Windows build) green — watch the `rlgl` calls on GL ES / older
   drivers; guard and fall back to the flat path if array textures are absent.
