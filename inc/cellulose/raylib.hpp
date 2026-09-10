#ifndef CEL_RAYLIB_HPP
#define CEL_RAYLIB_HPP

// Optional raylib bridge. NOT pulled in by <cellulose/cellulose.hpp> — the core
// library is renderer-free. Include this yourself (with raylib on the link line)
// when you want a ready-made ChunkMesh -> raylib Mesh upload.
//
// Two paths:
//   * to_raylib_mesh(mesh, color_fn)   — flat / vertex-coloured, no texture.
//   * to_raylib_mesh(mesh, atlas)      — textured: one 2-D atlas Texture2D plus
//     `load_atlas_shader` / `load_atlas_material`. The shader tiles each merged
//     greedy quad *within* its atlas tile (`origin + fract(uv) * tileSize`), so
//     no texture-array feature and no platform-specific GL is needed.

#include "mesh.hpp"
#include "texture.hpp"
#include "types.hpp"
#include <raylib.h>

namespace cellulose {

namespace impl {

/// @brief Fill (but do not upload) a raylib `Mesh` from a `ChunkMesh`.
/// `p_color_of` maps each vertex to its colour; `p_atlas` (when non-null) adds
/// a `texcoords2` stream carrying each vertex's tile origin.
template <typename ColorFn>
inline auto fill_raylib_mesh(const ChunkMesh &p_mesh, ColorFn &&p_color_of, const TextureAtlas *p_atlas) -> Mesh {
	Mesh mesh = { 0 };
	mesh.vertexCount = static_cast<int>(p_mesh.vertices.size());
	mesh.triangleCount = static_cast<int>(p_mesh.indices.size() / 3);

	mesh.vertices = static_cast<float *>(MemAlloc(sizeof(float) * 3 * mesh.vertexCount));
	mesh.normals = static_cast<float *>(MemAlloc(sizeof(float) * 3 * mesh.vertexCount));
	mesh.texcoords = static_cast<float *>(MemAlloc(sizeof(float) * 2 * mesh.vertexCount));
	mesh.colors = static_cast<unsigned char *>(MemAlloc(sizeof(unsigned char) * 4 * mesh.vertexCount));
	mesh.indices = static_cast<unsigned short *>(MemAlloc(sizeof(unsigned short) * 3 * mesh.triangleCount));
	if (p_atlas != nullptr)
		mesh.texcoords2 = static_cast<float *>(MemAlloc(sizeof(float) * 2 * mesh.vertexCount));

	for (int i = 0; i < mesh.vertexCount; ++i) {
		const MeshVertex &vertex = p_mesh.vertices[static_cast<size>(i)];
		mesh.vertices[i * 3 + 0] = vertex.position.x;
		mesh.vertices[i * 3 + 1] = vertex.position.y;
		mesh.vertices[i * 3 + 2] = vertex.position.z;
		mesh.normals[i * 3 + 0] = vertex.normal.x;
		mesh.normals[i * 3 + 1] = vertex.normal.y;
		mesh.normals[i * 3 + 2] = vertex.normal.z;
		mesh.texcoords[i * 2 + 0] = vertex.u;
		mesh.texcoords[i * 2 + 1] = vertex.v;

		const Color color = p_color_of(vertex);
		mesh.colors[i * 4 + 0] = color.r;
		mesh.colors[i * 4 + 1] = color.g;
		mesh.colors[i * 4 + 2] = color.b;
		mesh.colors[i * 4 + 3] = color.a;

		if (p_atlas != nullptr) {
			const UvRect rect = p_atlas->rect_of(vertex.texture_id);
			mesh.texcoords2[i * 2 + 0] = rect.u0;
			mesh.texcoords2[i * 2 + 1] = rect.v0;
		}
	}

	for (size i = 0; i < p_mesh.indices.size(); ++i)
		mesh.indices[i] = static_cast<unsigned short>(p_mesh.indices[i]);

	return mesh;
}

} //namespace impl

/// @brief Upload a `ChunkMesh` to a raylib `Mesh`; raylib owns the buffers, so
/// release it with `UnloadMesh` (or `UnloadModel` if wrapped). `p_color_of`
/// maps each `MeshVertex` to its vertex colour.
///
/// Indices are narrowed to `unsigned short` — keep a chunk mesh under 65 536
/// vertices (true for any single chunk in practice).
template <typename ColorFn>
inline auto to_raylib_mesh(const ChunkMesh &p_mesh, ColorFn &&p_color_of) -> Mesh {
	Mesh mesh = impl::fill_raylib_mesh(p_mesh, p_color_of, nullptr);
	UploadMesh(&mesh, false);
	return mesh;
}

/// @brief `to_raylib_mesh` with a default grayscale-by-face-brightness colour.
inline auto to_raylib_mesh(const ChunkMesh &p_mesh) -> Mesh {
	return to_raylib_mesh(p_mesh, [](const MeshVertex &p_vertex) {
		const auto shade = static_cast<unsigned char>(40.0f + 215.0f * p_vertex.brightness);
		return Color{ shade, shade, shade, 255 };
	});
}

// --- textured (atlas) path --------------------------------------------------

/// @brief GLSL 330 vertex shader for the atlas-tiling material: passes the
/// mesh's tile-space UVs and its tile's atlas origin (baked into `texcoords2`)
/// through to the fragment stage. `mvp` is auto-wired by raylib.
inline constexpr const char *atlas_tiling_vs = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec2 vertexTexCoord2;
in vec4 vertexColor;
uniform mat4 mvp;
out vec2 fragTileUV;
out vec2 fragTileOrigin;
out vec4 fragColor;
void main() {
	fragTileUV = vertexTexCoord;
	fragTileOrigin = vertexTexCoord2;
	fragColor = vertexColor;
	gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

/// @brief GLSL 330 fragment shader: tiles within the atlas tile with
/// `origin + fract(uv) * uTileSize`, then modulates by the per-vertex colour
/// (face brightness). `texture0` / `colDiffuse` are bound by raylib from the
/// material's diffuse map; `uTileSize` is set by `load_atlas_shader`.
inline constexpr const char *atlas_tiling_fs = R"(#version 330
in vec2 fragTileUV;
in vec2 fragTileOrigin;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec2 uTileSize;
out vec4 finalColor;
void main() {
	vec2 uv = fragTileOrigin + fract(fragTileUV) * uTileSize;
	finalColor = texture(texture0, uv) * fragColor * colDiffuse;
}
)";

/// @brief `ChunkMesh` -> textured raylib `Mesh` for the atlas-tiling material.
/// `texcoords` stay the tile-space `[0, quad]` UVs; `texcoords2` carries each
/// vertex's tile origin (`p_atlas.rect_of(texture_id)` top-left); the vertex
/// colour carries face brightness. Release with `UnloadMesh` / `UnloadModel`.
inline auto to_raylib_mesh(const ChunkMesh &p_mesh, const TextureAtlas &p_atlas) -> Mesh {
	Mesh mesh = impl::fill_raylib_mesh(
			p_mesh,
			[](const MeshVertex &p_vertex) {
				const auto shade = static_cast<unsigned char>((0.45f + 0.55f * p_vertex.brightness) * 255.0f);
				return Color{ shade, shade, shade, 255 };
			},
			&p_atlas);
	UploadMesh(&mesh, false);
	return mesh;
}

/// @brief Compile the atlas-tiling shader and set its `uTileSize` uniform from
/// `p_atlas` (the size of one tile in UV space — assumes a uniform grid, which
/// `TextureAtlas::grid` / `atlas_builder::strip` produce). Free with
/// `UnloadShader`.
inline auto load_atlas_shader(const TextureAtlas &p_atlas) -> Shader {
	Shader shader = LoadShaderFromMemory(atlas_tiling_vs, atlas_tiling_fs);
	const UvRect tile = p_atlas.rect_of(0);
	const float tile_size[2] = { tile.u1 - tile.u0, tile.v1 - tile.v0 };
	SetShaderValue(shader, GetShaderLocation(shader, "uTileSize"), tile_size, SHADER_UNIFORM_VEC2);
	return shader;
}

/// @brief A `Material` bound to `p_shader` with `p_atlas_texture` as its diffuse
/// map — ready for `DrawMesh(mesh, material, transform)`. Free with
/// `UnloadMaterial` (which also unloads the shader; unload the texture yourself).
inline auto load_atlas_material(Shader p_shader, Texture2D p_atlas_texture) -> Material {
	Material material = LoadMaterialDefault();
	material.shader = p_shader;
	material.maps[MATERIAL_MAP_DIFFUSE].texture = p_atlas_texture;
	material.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
	return material;
}

} //namespace cellulose

#endif // CEL_RAYLIB_HPP
