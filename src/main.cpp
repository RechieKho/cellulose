#include <raylib.h>
#include <cellulose/cellulose.hpp>
#include <cstdint>
#include <iostream>

namespace {

// Solidity predicate for the demo: any non-air block.
auto is_solid(const cellulose::HotCellAttribute &p_attribute) -> bool {
	return p_attribute.block_id != 0;
}

// Place a fully-lit block, optionally dimming its top face for a little contrast.
auto place(cellulose::World<> &p_world, int p_x, int p_y, int p_z, cellulose::BlockID p_id, cellulose::u8 p_top = 3) -> void {
	auto &attribute = p_world.chunk(cellulose::to_chunk_position(cellulose::WorldPosition{ p_x, p_y, p_z }))
							  .hot_attribute(cellulose::to_local_position(cellulose::WorldPosition{ p_x, p_y, p_z }));
	attribute.block_id = p_id;
	attribute.set_right_face_brightness(3);
	attribute.set_left_face_brightness(3);
	attribute.set_top_face_brightness(p_top);
	attribute.set_bottom_face_brightness(1);
	attribute.set_back_face_brightness(2);
	attribute.set_front_face_brightness(2);
}

// Convert a cellulose::ChunkMesh into a raylib Mesh (raylib owns the buffers).
auto to_raylib_mesh(const cellulose::ChunkMesh &p_mesh) -> Mesh {
	Mesh mesh = { 0 };
	mesh.vertexCount = static_cast<int>(p_mesh.vertices.size());
	mesh.triangleCount = static_cast<int>(p_mesh.indices.size() / 3);

	mesh.vertices = static_cast<float *>(MemAlloc(sizeof(float) * 3 * mesh.vertexCount));
	mesh.normals = static_cast<float *>(MemAlloc(sizeof(float) * 3 * mesh.vertexCount));
	mesh.texcoords = static_cast<float *>(MemAlloc(sizeof(float) * 2 * mesh.vertexCount));
	mesh.colors = static_cast<unsigned char *>(MemAlloc(sizeof(unsigned char) * 4 * mesh.vertexCount));
	mesh.indices = static_cast<unsigned short *>(MemAlloc(sizeof(unsigned short) * 3 * mesh.triangleCount));

	for (int i = 0; i < mesh.vertexCount; ++i) {
		const auto &vertex = p_mesh.vertices[static_cast<std::size_t>(i)];
		mesh.vertices[i * 3 + 0] = vertex.position.x;
		mesh.vertices[i * 3 + 1] = vertex.position.y;
		mesh.vertices[i * 3 + 2] = vertex.position.z;
		mesh.normals[i * 3 + 0] = vertex.normal.x;
		mesh.normals[i * 3 + 1] = vertex.normal.y;
		mesh.normals[i * 3 + 2] = vertex.normal.z;
		mesh.texcoords[i * 2 + 0] = vertex.u;
		mesh.texcoords[i * 2 + 1] = vertex.v;

		// Tint by block id, shade by face brightness (0.35 .. 1.0).
		const float shade = 0.35f + 0.65f * vertex.brightness;
		const unsigned char r = static_cast<unsigned char>((vertex.block_id == 1 ? 150.0f : 90.0f) * shade);
		const unsigned char g = static_cast<unsigned char>((vertex.block_id == 1 ? 190.0f : 130.0f) * shade);
		const unsigned char b = static_cast<unsigned char>((vertex.block_id == 1 ? 120.0f : 200.0f) * shade);
		mesh.colors[i * 4 + 0] = r;
		mesh.colors[i * 4 + 1] = g;
		mesh.colors[i * 4 + 2] = b;
		mesh.colors[i * 4 + 3] = 255;
	}

	for (std::size_t i = 0; i < p_mesh.indices.size(); ++i)
		mesh.indices[i] = static_cast<unsigned short>(p_mesh.indices[i]);

	UploadMesh(&mesh, false);
	return mesh;
}

} //namespace

int main() {
	const int screenWidth = 800;
	const int screenHeight = 450;

	cellulose::World<> world;

	// A small blocky scene in chunk (0,0,0): a floor slab plus a stepped tower.
	for (int x = 0; x < 8; ++x)
		for (int z = 0; z < 8; ++z)
			place(world, x, 0, z, 1, static_cast<cellulose::u8>((x + z) % 2 == 0 ? 3 : 2));
	for (int step = 0; step < 4; ++step)
		for (int x = 0; x <= step; ++x)
			for (int z = 0; z <= step; ++z)
				place(world, 2 + x, 1 + (3 - step), 2 + z, 2);

	std::cout << "chunks loaded: " << world.chunk_count() << std::endl;
	std::cout << "block at (2,4,2): "
			  << world.find_hot_attribute(cellulose::WorldPosition{ 2, 4, 2 })->block_id << std::endl;

	const cellulose::ChunkMesh chunk_mesh = cellulose::mesh_chunk(world, cellulose::ChunkPosition{ 0, 0, 0 }, is_solid);
	std::cout << "vertices: " << chunk_mesh.vertices.size() << std::endl;
	std::cout << "triangles: " << chunk_mesh.indices.size() / 3 << std::endl;

	InitWindow(screenWidth, screenHeight, "cellulose - greedy meshed chunk");

	Mesh mesh = to_raylib_mesh(chunk_mesh);
	Model model = LoadModelFromMesh(mesh);

	Camera3D camera = { 0 };
	camera.position = Vector3{ 18.0f, 16.0f, 18.0f };
	camera.target = Vector3{ 4.0f, 3.0f, 4.0f };
	camera.up = Vector3{ 0.0f, 1.0f, 0.0f };
	camera.fovy = 45.0f;
	camera.projection = CAMERA_PERSPECTIVE;

	SetTargetFPS(60);

	while (!WindowShouldClose()) {
		UpdateCamera(&camera, CAMERA_ORBITAL);

		BeginDrawing();
		ClearBackground(RAYWHITE);

		BeginMode3D(camera);
		DrawModel(model, Vector3{ 0.0f, 0.0f, 0.0f }, 1.0f, WHITE);
		DrawModelWires(model, Vector3{ 0.0f, 0.0f, 0.0f }, 1.0f, Fade(BLACK, 0.15f));
		DrawGrid(16, 1.0f);
		EndMode3D();

		DrawText("cellulose: one greedy-meshed 32^3 chunk", 10, 34, 20, DARKGRAY);
		DrawFPS(10, 10);

		EndDrawing();
	}

	UnloadModel(model); // frees the uploaded mesh too
	CloseWindow();

	return 0;
}
