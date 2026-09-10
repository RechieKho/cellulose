// A minimal voxel game on top of `cellulose` — fly around, hold LMB to mine a
// block, RMB to place one. The whole "engine" is:
//   * a `cellulose::World` holding the blocks,
//   * a `cellulose::BlockRegistry` mapping block ids to per-face textures,
//   * a custom cold-tier cell attribute (`Damage`) for mining progress,
//   * `cellulose::raycast` to find the block under the crosshair,
//   * `cellulose::mesh_chunk` (with baked ambient occlusion) to (re)build each
//     chunk's render mesh after an edit,
//   * `cellulose::to_raylib_mesh` + the atlas-tiling shader to draw it.
// Everything else here is raylib windowing / camera / draw calls.

#include <raylib.h>
#include <raymath.h>
#include <algorithm>
#include <array>
#include <cellulose/cellulose.hpp>
#include <cellulose/raylib.hpp>
#include <cmath>
#include <iostream>
#include <optional>

namespace {

constexpr int chunks = 3; // a `chunks` x `chunks` patch of terrain on the XZ plane
constexpr int edge = static_cast<int>(cellulose::chunk_edge_length); // 32
constexpr int tile_px = 16;

// Block ids (index into the registry) and texture ids (cells in the atlas grid).
enum : cellulose::u16 { block_air = 0,
	block_grass = 1,
	block_dirt = 2,
	block_stone = 3 };
enum : cellulose::TextureID { tex_grass_top = 1,
	tex_grass_side = 2,
	tex_dirt = 3,
	tex_stone = 4 };

// --- extending the cell with a custom attribute -----------------------------
// The hot tier is shipped (`HotCellAttribute` — block id + per-face state). The
// cold and freezing tiers are consumer-defined: you pick the types and cellulose
// stores one dense SoA array (cold) / a sparse map (freezing) per chunk. Here we
// add a single cold-tier byte: how far mined the block is.
struct Damage final {
	cellulose::u8 hits = 0;
};

// A chunk whose cold tier is exactly `{ Damage }`; freezing tier left empty.
using GameChunk = cellulose::Chunk<
		cellulose::HotCellAttribute,
		cellulose::PackedChunkAttributes<Damage>>;
using GameWorld = cellulose::World<GameChunk>;

constexpr cellulose::u8 hits_to_break = 5;

// "Solid" for raycasting and meshing: any non-air block id.
auto is_solid(const cellulose::HotCellAttribute &p_attribute) -> bool {
	return p_attribute.block_id != block_air;
}

// Read the mining progress of one cell through the cold-tier seqlock.
auto damage_of(GameWorld &p_world, const cellulose::WorldPosition &p_cell) -> cellulose::u8 {
	auto *chunk = p_world.find_chunk(cellulose::to_chunk_position(p_cell));
	if (chunk == nullptr)
		return 0;
	const auto index = cellulose::encode_cell_index(cellulose::to_local_position(p_cell));
	return chunk->read_cold([&](const auto &p_packed) {
		return p_packed.template get<Damage>()[index].hits;
	});
}

// Write the mining progress of one cell as the sole cold-tier writer.
auto set_damage(GameWorld &p_world, const cellulose::WorldPosition &p_cell, cellulose::u8 p_hits) -> void {
	auto *chunk = p_world.find_chunk(cellulose::to_chunk_position(p_cell));
	if (chunk == nullptr)
		return;
	const auto index = cellulose::encode_cell_index(cellulose::to_local_position(p_cell));
	chunk->write_cold([&](auto &p_packed) {
		p_packed.template get<Damage>()[index].hits = p_hits;
	});
}

// Paint a procedural atlas image matching `p_packed`'s grid — tile id `n` lands
// in cell `(n % columns, n / columns)`, exactly where `TextureAtlas::rect_of`
// says it is.
auto build_atlas_image(const cellulose::PackedAtlas &p_packed) -> Image {
	Image image = GenImageColor(static_cast<int>(p_packed.width), static_cast<int>(p_packed.height), BLANK);
	const int columns = static_cast<int>(p_packed.atlas.columns());
	const auto jitter = [](unsigned char p_channel, int p_delta) {
		return static_cast<unsigned char>(std::min(255, std::max(0, static_cast<int>(p_channel) + p_delta)));
	};
	const auto fill = [&](cellulose::TextureID p_id, Color p_base, int p_cap_rows, Color p_cap) {
		const int ox = (static_cast<int>(p_id) % columns) * tile_px;
		const int oy = (static_cast<int>(p_id) / columns) * tile_px;
		for (int y = 0; y < tile_px; ++y)
			for (int x = 0; x < tile_px; ++x) {
				const int n = (x * 7 + y * 13) % 24 - 12; // cheap per-texel noise
				Color c = (y < p_cap_rows) ? p_cap : p_base;
				c.r = jitter(c.r, n);
				c.g = jitter(c.g, n);
				c.b = jitter(c.b, n);
				ImageDrawPixel(&image, ox + x, oy + y, c);
			}
	};
	fill(tex_grass_top, Color{ 96, 172, 72, 255 }, 0, WHITE);
	fill(tex_grass_side, Color{ 148, 108, 74, 255 }, 4, Color{ 96, 172, 72, 255 });
	fill(tex_dirt, Color{ 148, 108, 74, 255 }, 0, WHITE);
	fill(tex_stone, Color{ 128, 128, 134, 255 }, 0, WHITE);
	return image;
}

// A gentle sine-wave heightmap so there's terrain to dig into.
auto generate(GameWorld &p_world) -> void {
	for (int cx = 0; cx < chunks; ++cx)
		for (int cz = 0; cz < chunks; ++cz) {
			auto &chunk = p_world.chunk(cellulose::ChunkPosition{ cx, 0, cz });
			for (int lx = 0; lx < edge; ++lx)
				for (int lz = 0; lz < edge; ++lz) {
					const int wx = cx * edge + lx;
					const int wz = cz * edge + lz;
					const int height = 8 + static_cast<int>(3.0 * std::sin(wx * 0.15) * std::cos(wz * 0.15));
					for (int y = 0; y <= height && y < edge; ++y) {
						const cellulose::u16 id = (y == height) ? block_grass : (y + 3 > height ? block_dirt : block_stone);
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
	GameWorld world;
	generate(world);
	std::cout << "world: " << world.chunk_count() << " chunks generated\n";

	// Block id -> per-face texture ids. `grass` is a Minecraft-style column.
	const auto registry =
			cellulose::BlockRegistryBuilder()
					.add_block_builder(cellulose::BlockBuilder("air"))
					.add_block_builder(cellulose::BlockBuilder("grass").texture_column(tex_grass_top, tex_grass_side, tex_dirt))
					.add_block_builder(cellulose::BlockBuilder("dirt").texture_all(tex_dirt))
					.add_block_builder(cellulose::BlockBuilder("stone").texture_all(tex_stone))
					.build();

	SetConfigFlags(FLAG_MSAA_4X_HINT); // smooth the silhouettes; T-junctions are welded in the mesh
	InitWindow(1280, 720, "cellulose - minimal voxel game");
	DisableCursor();
	SetTargetFPS(60);

	const std::array<cellulose::TextureID, 4> ids{ tex_grass_top, tex_grass_side, tex_dirt, tex_stone };
	const cellulose::PackedAtlas packed = cellulose::pack_grid(ids, tile_px);
	const cellulose::TextureAtlas &atlas = packed.atlas;

	Image atlas_image = build_atlas_image(packed);
	const Texture2D atlas_texture = LoadTextureFromImage(atlas_image);
	UnloadImage(atlas_image);
	SetTextureFilter(atlas_texture, TEXTURE_FILTER_POINT);
	SetTextureWrap(atlas_texture, TEXTURE_WRAP_CLAMP);

	const Shader shader = cellulose::load_atlas_shader(atlas);
	const Material material = cellulose::load_atlas_material(shader, atlas_texture);

	// One raylib Mesh per terrain chunk, rebuilt when marked dirty.
	std::array<std::array<Mesh, chunks>, chunks> meshes{};
	std::array<std::array<bool, chunks>, chunks> has_mesh{};
	std::array<std::array<bool, chunks>, chunks> dirty{};
	for (auto &row : dirty)
		row.fill(true);

	const auto rebuild = [&](cellulose::i32 p_cx, cellulose::i32 p_cz) {
		if (!in_grid(p_cx, p_cz))
			return;
		const auto ux = static_cast<size_t>(p_cx);
		const auto uz = static_cast<size_t>(p_cz);
		if (has_mesh[ux][uz]) {
			UnloadMesh(meshes[ux][uz]);
			has_mesh[ux][uz] = false;
		}
		const cellulose::ChunkMesh chunk_mesh = cellulose::mesh_chunk(
				world, cellulose::ChunkPosition{ p_cx, 0, p_cz }, is_solid, registry,
				cellulose::MeshOptions{ .ambient_occlusion = true, .weld_t_junctions = true });
		if (!chunk_mesh.empty()) {
			meshes[ux][uz] = cellulose::to_raylib_mesh(chunk_mesh, atlas);
			has_mesh[ux][uz] = true;
		}
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

	// The block currently being mined; its `Damage` resets if we stop or look away.
	std::optional<cellulose::WorldPosition> mining;
	float mine_timer = 0.0f;

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

		// --- mine (hold LMB) / place (RMB) ---------------------------------
		mine_timer -= GetFrameTime();
		const bool want_mine = hit.has_value() && IsMouseButtonDown(MOUSE_BUTTON_LEFT);

		// Stopped mining, or the crosshair moved to another block: reset the
		// half-mined block's Damage (Minecraft-style).
		if (mining.has_value() && !(want_mine && mining.value() == hit->cell)) {
			set_damage(world, mining.value(), 0);
			mining.reset();
		}

		if (want_mine && mine_timer <= 0.0f) {
			mine_timer = 0.15f;
			mining = hit->cell;
			const auto hits = static_cast<cellulose::u8>(damage_of(world, hit->cell) + 1);
			set_damage(world, hit->cell, hits); // read-modify-write the custom attribute
			if (hits >= hits_to_break) {
				if (auto *chunk = world.find_chunk(cellulose::to_chunk_position(hit->cell))) {
					chunk->hot_attribute(cellulose::to_local_position(hit->cell)).block_id = block_air;
					touch(hit->cell);
				}
				set_damage(world, hit->cell, 0);
				mining.reset();
			}
		} else if (hit.has_value() && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
			const cellulose::WorldPosition against{
				hit->cell.x + hit->normal.x,
				hit->cell.y + hit->normal.y,
				hit->cell.z + hit->normal.z
			};
			const cellulose::ChunkPosition cp = cellulose::to_chunk_position(against);
			if (in_grid(cp.x, cp.z) && cp.y == 0) {
				world.chunk(cp).hot_attribute(cellulose::to_local_position(against)).block_id = block_grass;
				touch(against);
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
				const auto ux = static_cast<size_t>(cx);
				const auto uz = static_cast<size_t>(cz);
				if (has_mesh[ux][uz])
					DrawMesh(meshes[ux][uz], material,
							MatrixTranslate(static_cast<float>(cx * edge), 0.0f, static_cast<float>(cz * edge)));
			}
		cellulose::u8 aimed_damage = 0;
		if (hit.has_value()) {
			aimed_damage = damage_of(world, hit->cell); // read the custom attribute
			const auto redness = static_cast<unsigned char>(255 * aimed_damage / hits_to_break);
			DrawCubeWires(
					Vector3{ hit->cell.x + 0.5f, hit->cell.y + 0.5f, hit->cell.z + 0.5f },
					1.02f, 1.02f, 1.02f, Color{ redness, 40, 40, 255 });
		}
		EndMode3D();

		const int cx = GetScreenWidth() / 2;
		const int cy = GetScreenHeight() / 2;
		DrawLine(cx - 8, cy, cx + 8, cy, WHITE);
		DrawLine(cx, cy - 8, cx, cy + 8, WHITE);
		DrawText("WASD + mouse to fly   hold LMB to mine   RMB place", 12, 12, 20, RAYWHITE);
		if (aimed_damage > 0)
			DrawText(TextFormat("mining  %d / %d", aimed_damage, hits_to_break), 12, 36, 20, RAYWHITE);
		DrawFPS(12, GetScreenHeight() - 28);

		EndDrawing();
	}

	for (cellulose::i32 cx = 0; cx < chunks; ++cx)
		for (cellulose::i32 cz = 0; cz < chunks; ++cz)
			if (has_mesh[static_cast<size_t>(cx)][static_cast<size_t>(cz)])
				UnloadMesh(meshes[static_cast<size_t>(cx)][static_cast<size_t>(cz)]);
	UnloadMaterial(material); // also unloads the shader
	UnloadTexture(atlas_texture);
	CloseWindow();
	return 0;
}
