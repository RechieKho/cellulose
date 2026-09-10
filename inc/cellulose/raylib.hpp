#ifndef CEL_RAYLIB_HPP
#define CEL_RAYLIB_HPP

// Optional raylib bridge. NOT pulled in by <cellulose/cellulose.hpp> — the core
// library is renderer-free. Include this yourself (with raylib on the link line)
// when you want a ready-made ChunkMesh -> raylib Mesh upload.

#include "mesh.hpp"
#include "types.hpp"
#include <raylib.h>

namespace cellulose {

/// @brief Upload a `ChunkMesh` to a raylib `Mesh`; raylib owns the buffers, so
/// release it with `UnloadMesh` (or `UnloadModel` if wrapped). `p_color_of`
/// maps each `MeshVertex` to its vertex colour.
///
/// Indices are narrowed to `unsigned short` — keep a chunk mesh under 65 536
/// vertices (true for any single chunk in practice).
template <typename ColorFn>
inline auto to_raylib_mesh(const ChunkMesh &p_mesh, ColorFn &&p_color_of) -> Mesh {
	Mesh mesh = { 0 };
	mesh.vertexCount = static_cast<int>(p_mesh.vertices.size());
	mesh.triangleCount = static_cast<int>(p_mesh.indices.size() / 3);

	mesh.vertices = static_cast<float *>(MemAlloc(sizeof(float) * 3 * mesh.vertexCount));
	mesh.normals = static_cast<float *>(MemAlloc(sizeof(float) * 3 * mesh.vertexCount));
	mesh.texcoords = static_cast<float *>(MemAlloc(sizeof(float) * 2 * mesh.vertexCount));
	mesh.colors = static_cast<unsigned char *>(MemAlloc(sizeof(unsigned char) * 4 * mesh.vertexCount));
	mesh.indices = static_cast<unsigned short *>(MemAlloc(sizeof(unsigned short) * 3 * mesh.triangleCount));

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
	}

	for (size i = 0; i < p_mesh.indices.size(); ++i)
		mesh.indices[i] = static_cast<unsigned short>(p_mesh.indices[i]);

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

} //namespace cellulose

#endif // CEL_RAYLIB_HPP
