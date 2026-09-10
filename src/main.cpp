// A minimal voxel game on top of `cellulose` — fly around, break blocks with the
// left mouse button, place them with the right. The whole "engine" is:
//   * a `cellulose::World` holding the blocks,
//   * `cellulose::raycast` to find the block under the crosshair,
//   * `cellulose::mesh_chunk` to (re)build each chunk's render mesh after an edit.
// Everything else here is raylib windowing / camera / draw calls.

#include <raylib.h>
#include <array>
#include <cellulose/cellulose.hpp>
#include <cellulose/raylib.hpp>
#include <cmath>
#include <iostream>

namespace {

constexpr int chunks = 3; // a `chunks` x `chunks` patch of terrain on the XZ plane
constexpr int edge = static_cast<int>(cellulose::chunk_edge_length); // 32

// "Solid" for raycasting and meshing: any non-air block id.
auto is_solid(const cellulose::HotCellAttribute &p_attribute) -> bool {
	return p_attribute.block_id != 0;
}

// block id → colour, shaded by the mesher's per-face brightness.
auto block_colour(const cellulose::MeshVertex &p_vertex) -> Color {
	const float shade = 0.45f + 0.55f * p_vertex.brightness;
	const auto scale = [shade](float p_value) { return static_cast<unsigned char>(p_value * shade); };
	switch (p_vertex.block_id) {
		case 1:
			return Color{ scale(96), scale(172), scale(72), 255 }; // grass
		case 2:
			return Color{ scale(148), scale(108), scale(74), 255 }; // dirt
		default:
			return Color{ scale(128), scale(128), scale(134), 255 }; // stone
	}
}

// A gentle sine-wave heightmap so there's terrain to dig into.
auto generate(cellulose::World<> &p_world) -> void {
	for (int cx = 0; cx < chunks; ++cx)
		for (int cz = 0; cz < chunks; ++cz) {
			auto &chunk = p_world.chunk(cellulose::ChunkPosition{ cx, 0, cz });
			for (int lx = 0; lx < edge; ++lx)
				for (int lz = 0; lz < edge; ++lz) {
					const int wx = cx * edge + lx;
					const int wz = cz * edge + lz;
					const int height = 8 + static_cast<int>(3.0 * std::sin(wx * 0.15) * std::cos(wz * 0.15));
					for (int y = 0; y <= height && y < edge; ++y) {
						const cellulose::u16 id = (y == height) ? 1 : (y + 3 > height ? 2 : 3);
						chunk.hot_attribute(cellulose::LocalPosition{
													static_cast<cellulose::u8>(lx),
													static_cast<cellulose::u8>(y),
													static_cast<cellulose::u8>(lz) })
								.block_id = id;
					}
				}
		}
}

// True if (cx, cz) is one of our loaded terrain chunks.
auto in_grid(cellulose::i32 p_cx, cellulose::i32 p_cz) -> bool {
	return p_cx >= 0 && p_cx < chunks && p_cz >= 0 && p_cz < chunks;
}

} //namespace

auto main() -> int {
	cellulose::World<> world;
	generate(world);
	std::cout << "world: " << world.chunk_count() << " chunks generated\n";

	InitWindow(1280, 720, "cellulose - minimal voxel game");
	DisableCursor();
	SetTargetFPS(60);

	// One raylib Model per terrain chunk, rebuilt when marked dirty.
	std::array<std::array<Model, chunks>, chunks> models{};
	std::array<std::array<bool, chunks>, chunks> dirty{};
	for (auto &row : dirty)
		row.fill(true);

	const auto rebuild = [&](cellulose::i32 p_cx, cellulose::i32 p_cz) {
		if (!in_grid(p_cx, p_cz))
			return;
		Model &slot = models[static_cast<size_t>(p_cx)][static_cast<size_t>(p_cz)];
		if (slot.meshCount > 0)
			UnloadModel(slot);
		slot = Model{};
		const cellulose::ChunkMesh mesh = cellulose::mesh_chunk(
				world, cellulose::ChunkPosition{ p_cx, 0, p_cz }, is_solid);
		if (!mesh.empty())
			slot = LoadModelFromMesh(cellulose::to_raylib_mesh(mesh, block_colour));
	};

	// Mark an edited cell's chunk dirty, plus a neighbour chunk if the cell sits
	// on a chunk face (that face's culling in the neighbour just changed).
	const auto touch = [&](const cellulose::WorldPosition &p_cell) {
		const cellulose::ChunkPosition cp = cellulose::to_chunk_position(p_cell);
		const cellulose::LocalPosition lp = cellulose::to_local_position(p_cell);
		const auto mark = [&](cellulose::i32 p_cx, cellulose::i32 p_cz) {
			if (in_grid(p_cx, p_cz))
				dirty[static_cast<size_t>(p_cx)][static_cast<size_t>(p_cz)] = true;
		};
		mark(cp.x, cp.z);
		if (lp.x == 0)
			mark(cp.x - 1, cp.z);
		if (lp.x == edge - 1)
			mark(cp.x + 1, cp.z);
		if (lp.z == 0)
			mark(cp.x, cp.z - 1);
		if (lp.z == edge - 1)
			mark(cp.x, cp.z + 1);
	};

	Camera3D camera = { 0 };
	camera.position = Vector3{ edge * chunks * 0.5f, 20.0f, -4.0f };
	camera.target = Vector3{ edge * chunks * 0.5f, 14.0f, 8.0f };
	camera.up = Vector3{ 0.0f, 1.0f, 0.0f };
	camera.fovy = 70.0f;
	camera.projection = CAMERA_PERSPECTIVE;

	while (!WindowShouldClose()) {
		// --- fly camera (raylib does the math; no gravity) --------------------
		const float speed = 12.0f * GetFrameTime();
		const Vector3 move{
			static_cast<float>(IsKeyDown(KEY_W) - IsKeyDown(KEY_S)) * speed,
			static_cast<float>(IsKeyDown(KEY_D) - IsKeyDown(KEY_A)) * speed,
			static_cast<float>(IsKeyDown(KEY_SPACE) - IsKeyDown(KEY_LEFT_SHIFT)) * speed
		};
		const Vector3 look{ GetMouseDelta().x * 0.15f, GetMouseDelta().y * 0.15f, 0.0f };
		UpdateCameraPro(&camera, move, look, 0.0f);

		// --- what block is under the crosshair? ------------------------------
		const cellulose::Ray ray{
			{ camera.position.x, camera.position.y, camera.position.z },
			{ camera.target.x - camera.position.x,
					camera.target.y - camera.position.y,
					camera.target.z - camera.position.z }
		};
		const auto hit = cellulose::raycast(world, ray, 8.0, is_solid);

		// --- break / place --------------------------------------------------
		if (hit.has_value()) {
			if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
				if (auto *chunk = world.find_chunk(cellulose::to_chunk_position(hit->cell))) {
					chunk->hot_attribute(cellulose::to_local_position(hit->cell)).block_id = 0;
					touch(hit->cell);
				}
			} else if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
				const cellulose::WorldPosition against{
					hit->cell.x + hit->normal.x,
					hit->cell.y + hit->normal.y,
					hit->cell.z + hit->normal.z
				};
				const cellulose::ChunkPosition cp = cellulose::to_chunk_position(against);
				if (in_grid(cp.x, cp.z) && cp.y == 0) {
					world.chunk(cp).hot_attribute(cellulose::to_local_position(against)).block_id = 1;
					touch(against);
				}
			}
		}

		for (cellulose::i32 cx = 0; cx < chunks; ++cx)
			for (cellulose::i32 cz = 0; cz < chunks; ++cz)
				if (dirty[static_cast<size_t>(cx)][static_cast<size_t>(cz)]) {
					rebuild(cx, cz);
					dirty[static_cast<size_t>(cx)][static_cast<size_t>(cz)] = false;
				}

		// --- draw ---------------------------------------------------------
		BeginDrawing();
		ClearBackground(SKYBLUE);

		BeginMode3D(camera);
		for (int cx = 0; cx < chunks; ++cx)
			for (int cz = 0; cz < chunks; ++cz) {
				const Model &slot = models[static_cast<size_t>(cx)][static_cast<size_t>(cz)];
				if (slot.meshCount > 0)
					DrawModel(slot, Vector3{ static_cast<float>(cx * edge), 0.0f, static_cast<float>(cz * edge) }, 1.0f, WHITE);
			}
		if (hit.has_value())
			DrawCubeWires(
					Vector3{ hit->cell.x + 0.5f, hit->cell.y + 0.5f, hit->cell.z + 0.5f },
					1.02f, 1.02f, 1.02f, BLACK);
		EndMode3D();

		const int cx = GetScreenWidth() / 2;
		const int cy = GetScreenHeight() / 2;
		DrawLine(cx - 8, cy, cx + 8, cy, WHITE);
		DrawLine(cx, cy - 8, cx, cy + 8, WHITE);
		DrawText("WASD + mouse to fly   LMB break   RMB place", 12, 12, 20, RAYWHITE);
		DrawFPS(12, GetScreenHeight() - 28);

		EndDrawing();
	}

	for (auto &row : models)
		for (Model &slot : row)
			if (slot.meshCount > 0)
				UnloadModel(slot);
	CloseWindow();
	return 0;
}
