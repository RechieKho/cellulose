#include <doctest/doctest.h>

#include <cellulose/mesh.hpp>

#include <array>
#include <vector>

namespace {

using cellulose::i32;
using cellulose::u16;
using cellulose::u8;

constexpr std::array<std::array<i32, 3>, 6> face_offset{
	std::array<i32, 3>{ 1, 0, 0 }, std::array<i32, 3>{ -1, 0, 0 },
	std::array<i32, 3>{ 0, 1, 0 }, std::array<i32, 3>{ 0, -1, 0 },
	std::array<i32, 3>{ 0, 0, 1 }, std::array<i32, 3>{ 0, 0, -1 }
};

// A solid/attribute grid that computes face visibility the way the mesher does.
struct Grid final {
	i32 size;
	std::vector<bool> solid;
	std::vector<u16> block;
	std::array<std::vector<u8>, 6> brightness;

	explicit Grid(i32 p_size) :
			size(p_size),
			solid(static_cast<std::size_t>(p_size) * p_size * p_size, false),
			block(static_cast<std::size_t>(p_size) * p_size * p_size, 0) {
		for (auto &face : brightness)
			face.assign(static_cast<std::size_t>(size) * size * size, 3);
	}

	auto index(i32 p_x, i32 p_y, i32 p_z) const -> std::size_t {
		return static_cast<std::size_t>((p_x * size + p_y) * size + p_z);
	}
	auto in_bounds(i32 p_x, i32 p_y, i32 p_z) const -> bool {
		return p_x >= 0 && p_x < size && p_y >= 0 && p_y < size && p_z >= 0 && p_z < size;
	}

	auto set_solid(i32 p_x, i32 p_y, i32 p_z, u16 p_block_id = 1) -> void {
		solid[index(p_x, p_y, p_z)] = true;
		block[index(p_x, p_y, p_z)] = p_block_id;
	}
	auto set_face_brightness(i32 p_x, i32 p_y, i32 p_z, i32 p_face, u8 p_value) -> void {
		brightness[static_cast<std::size_t>(p_face)][index(p_x, p_y, p_z)] = p_value;
	}

	auto mesh() -> cellulose::ChunkMesh {
		std::vector<cellulose::MeshSample> samples(static_cast<std::size_t>(size) * size * size);
		for (i32 x = 0; x < size; ++x)
			for (i32 y = 0; y < size; ++y)
				for (i32 z = 0; z < size; ++z) {
					if (!solid[index(x, y, z)])
						continue;
					cellulose::MeshSample &sample = samples[index(x, y, z)];
					sample.block_id = block[index(x, y, z)];
					for (i32 face = 0; face < 6; ++face) {
						sample.brightness[static_cast<std::size_t>(face)] =
								brightness[static_cast<std::size_t>(face)][index(x, y, z)];
						const i32 nx = x + face_offset[static_cast<std::size_t>(face)][0];
						const i32 ny = y + face_offset[static_cast<std::size_t>(face)][1];
						const i32 nz = z + face_offset[static_cast<std::size_t>(face)][2];
						const bool hidden = in_bounds(nx, ny, nz) && solid[index(nx, ny, nz)];
						sample.visible[static_cast<std::size_t>(face)] = !hidden;
					}
				}
		return cellulose::greedy_mesh(samples, size, 1.0f);
	}
};

auto quads_facing(const cellulose::ChunkMesh &p_mesh, const cellulose::Vec3 &p_normal) -> int {
	int vertices = 0;
	for (const auto &vertex : p_mesh.vertices)
		if (vertex.normal == p_normal)
			++vertices;
	return vertices / 4;
}

auto has_vertex_at(const cellulose::ChunkMesh &p_mesh, const cellulose::Vec3 &p_position) -> bool {
	for (const auto &vertex : p_mesh.vertices)
		if (vertex.position == p_position)
			return true;
	return false;
}

} //namespace

// --- greedy_mesh (grid level) --------------------------------------------------

TEST_CASE("an empty grid meshes to nothing") {
	Grid grid(8);
	CHECK(grid.mesh().empty());
}

TEST_CASE("a lone solid cell meshes to six unit quads") {
	Grid grid(8);
	grid.set_solid(3, 3, 3);

	const auto mesh = grid.mesh();
	CHECK(mesh.vertices.size() == 24);
	CHECK(mesh.indices.size() == 36);

	for (const auto &normal : { cellulose::Vec3{ 1, 0, 0 }, cellulose::Vec3{ -1, 0, 0 },
				 cellulose::Vec3{ 0, 1, 0 }, cellulose::Vec3{ 0, -1, 0 },
				 cellulose::Vec3{ 0, 0, 1 }, cellulose::Vec3{ 0, 0, -1 } })
		CHECK(quads_facing(mesh, normal) == 1);

	for (const auto &vertex : mesh.vertices) {
		CHECK(vertex.position.x >= 3.0f);
		CHECK(vertex.position.x <= 4.0f);
	}
}

TEST_CASE("a 2x2x2 block still meshes to six quads (each face merged)") {
	Grid grid(8);
	for (i32 x = 0; x <= 1; ++x)
		for (i32 y = 0; y <= 1; ++y)
			for (i32 z = 0; z <= 1; ++z)
				grid.set_solid(x, y, z);

	const auto mesh = grid.mesh();
	CHECK(mesh.vertices.size() == 24);
	CHECK(has_vertex_at(mesh, cellulose::Vec3{ 2, 2, 2 }));
}

TEST_CASE("an interior shared face is culled") {
	Grid grid(8);
	grid.set_solid(3, 3, 3);
	grid.set_solid(4, 3, 3);

	const auto mesh = grid.mesh();
	CHECK(quads_facing(mesh, cellulose::Vec3{ 1, 0, 0 }) == 1);
	CHECK(quads_facing(mesh, cellulose::Vec3{ -1, 0, 0 }) == 1);
	CHECK(mesh.vertices.size() == 24); // 2x1x1 box -> six quads
}

TEST_CASE("differing face brightness prevents that face from merging") {
	Grid grid(4);
	grid.set_solid(1, 1, 1);
	grid.set_solid(2, 1, 1);
	grid.set_face_brightness(1, 1, 1, 2, 1); // +Y face
	grid.set_face_brightness(2, 1, 1, 2, 3);

	const auto mesh = grid.mesh();
	CHECK(quads_facing(mesh, cellulose::Vec3{ 0, 1, 0 }) == 2); // +Y split
	CHECK(quads_facing(mesh, cellulose::Vec3{ 0, -1, 0 }) == 1); // -Y still merged
	CHECK(mesh.vertices.size() == 28); // 7 quads
}

TEST_CASE("a +X quad is wound so its triangles face +X") {
	Grid grid(8);
	grid.set_solid(3, 3, 3);
	const auto mesh = grid.mesh();

	for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
		const auto &a = mesh.vertices[mesh.indices[t]];
		const auto &b = mesh.vertices[mesh.indices[t + 1]];
		const auto &c = mesh.vertices[mesh.indices[t + 2]];
		if (!(a.normal == cellulose::Vec3{ 1, 0, 0 }))
			continue;
		const auto face = cellulose::cross(b.position - a.position, c.position - a.position);
		CHECK(face.x > 0.0f);
		CHECK(face.y == doctest::Approx(0.0f));
		CHECK(face.z == doctest::Approx(0.0f));
		return;
	}
	FAIL("no +X triangle found");
}

// --- World-backed meshing -----------------------------------------------------

namespace {

auto world_solid() {
	return [](const cellulose::HotCellAttribute &p_attribute) { return p_attribute.block_id != 0; };
}

auto put(cellulose::World<> &p_world, cellulose::i64 p_x, cellulose::i64 p_y, cellulose::i64 p_z, u16 p_block_id = 1) -> void {
	const cellulose::WorldPosition cell{ p_x, p_y, p_z };
	p_world.chunk(cellulose::to_chunk_position(cell))
			.hot_attribute(cellulose::to_local_position(cell))
			.block_id = p_block_id;
}

} //namespace

TEST_CASE("mesh_chunk on an empty chunk produces nothing") {
	cellulose::World<> world;
	world.chunk({ 0, 0, 0 });
	CHECK(cellulose::mesh_chunk(world, { 0, 0, 0 }, world_solid()).empty());
}

TEST_CASE("mesh_chunk meshes a lone cell at its world-local position") {
	cellulose::World<> world;
	put(world, 5, 5, 5);

	const auto mesh = cellulose::mesh_chunk(world, { 0, 0, 0 }, world_solid());
	CHECK(mesh.vertices.size() == 24);
	for (const auto &vertex : mesh.vertices) {
		CHECK(vertex.position.x >= 5.0f);
		CHECK(vertex.position.x <= 6.0f);
	}
}

TEST_CASE("a fully solid chunk meshes to its six-quad shell") {
	cellulose::World<> world;
	for (cellulose::i64 x = 0; x < 32; ++x)
		for (cellulose::i64 y = 0; y < 32; ++y)
			for (cellulose::i64 z = 0; z < 32; ++z)
				put(world, x, y, z);

	const auto mesh = cellulose::mesh_chunk(world, { 0, 0, 0 }, world_solid());
	CHECK(mesh.vertices.size() == 24);
	CHECK(has_vertex_at(mesh, cellulose::Vec3{ 32, 32, 32 }));
}

TEST_CASE("mesh_chunk culls a face against a solid in the neighbouring chunk") {
	cellulose::World<> world;
	put(world, 31, 0, 0);
	put(world, 32, 0, 0); // chunk (1,0,0)

	const auto mesh = cellulose::mesh_chunk(world, { 0, 0, 0 }, world_solid());
	CHECK(quads_facing(mesh, cellulose::Vec3{ 1, 0, 0 }) == 0);
}

TEST_CASE("mesh_chunk keeps a boundary face when the neighbour chunk is absent") {
	cellulose::World<> world;
	put(world, 31, 0, 0);

	const auto mesh = cellulose::mesh_chunk(world, { 0, 0, 0 }, world_solid());
	CHECK(quads_facing(mesh, cellulose::Vec3{ 1, 0, 0 }) == 1);
}

TEST_CASE("mesh_chunk_lod level 0 equals mesh_chunk") {
	cellulose::World<> world;
	put(world, 2, 2, 2);
	put(world, 3, 2, 2);
	put(world, 10, 5, 7);

	CHECK(cellulose::mesh_chunk_lod(world, { 0, 0, 0 }, 0, world_solid()).vertices.size() ==
			cellulose::mesh_chunk(world, { 0, 0, 0 }, world_solid()).vertices.size());
}

TEST_CASE("mesh_chunk_lod collapses a full chunk to one shell at the coarsest level") {
	cellulose::World<> world;
	for (cellulose::i64 x = 0; x < 32; ++x)
		for (cellulose::i64 y = 0; y < 32; ++y)
			for (cellulose::i64 z = 0; z < 32; ++z)
				put(world, x, y, z);

	const auto mesh = cellulose::mesh_chunk_lod(world, { 0, 0, 0 }, 5, world_solid());
	CHECK(mesh.vertices.size() == 24);
	CHECK(has_vertex_at(mesh, cellulose::Vec3{ 32, 32, 32 }));
}

TEST_CASE("mesh_chunk_lod grows a lone cell into its macro-block") {
	cellulose::World<> world;
	put(world, 0, 0, 0);

	const auto mesh = cellulose::mesh_chunk_lod(world, { 0, 0, 0 }, 1, world_solid());
	CHECK(mesh.vertices.size() == 24);
	CHECK(has_vertex_at(mesh, cellulose::Vec3{ 2, 2, 2 }));
}

// --- Custom hot attribute type ----------------------------------------------

namespace {
struct TinyHot final {
	cellulose::BlockID block_id = 0;
};
static_assert(cellulose::HotAttribute<TinyHot>);
static_assert(sizeof(TinyHot) == 2);
} //namespace

TEST_CASE("a chunk with a custom 2-byte hot type meshes (flat-shaded)") {
	cellulose::World<cellulose::Chunk<TinyHot>> world;
	world.chunk({ 0, 0, 0 }).hot_attribute(cellulose::LocalPosition{ 4, 4, 4 }).block_id = 7;

	const auto mesh = cellulose::mesh_chunk(
			world, { 0, 0, 0 },
			[](const TinyHot &p_attribute) { return p_attribute.block_id != 0; });

	CHECK(mesh.vertices.size() == 24);
	for (const auto &vertex : mesh.vertices) {
		CHECK(vertex.block_id == 7);
		CHECK(vertex.brightness == doctest::Approx(1.0f)); // no face_brightness overload -> full
	}
}

// --- #8: split geometry / face-culling rules --------------------------------

TEST_CASE("mesh_chunk with explicit rules keeps a face between unlike transparent blocks") {
	cellulose::World<> world;
	put(world, 5, 5, 5, 1); // "glass"
	put(world, 6, 5, 5, 2); // "water"

	const auto has_geometry = [](const cellulose::HotCellAttribute &a) { return a.block_id != 0; };
	// a face is hidden only by the *same* block id (glass-glass, water-water)
	const auto is_hidden = [](const cellulose::HotCellAttribute &near, const cellulose::HotCellAttribute &far) {
		return far.block_id != 0 && far.block_id == near.block_id;
	};

	const auto mesh = cellulose::mesh_chunk(world, { 0, 0, 0 }, has_geometry, is_hidden);
	// the glass|water interface is NOT culled -> both +X (from cell 5) and -X (from cell 6) survive
	CHECK(quads_facing(mesh, cellulose::Vec3{ 1, 0, 0 }) == 2);
	CHECK(quads_facing(mesh, cellulose::Vec3{ -1, 0, 0 }) == 2);
}

// --- texture ids ------------------------------------------------------------

TEST_CASE("without a resolver every vertex's texture_id is its block_id") {
	cellulose::World<> world;
	put(world, 5, 5, 5, 9);

	const auto mesh = cellulose::mesh_chunk(world, { 0, 0, 0 }, world_solid());
	CHECK(mesh.vertices.size() == 24);
	for (const auto &vertex : mesh.vertices) {
		CHECK(vertex.block_id == 9);
		CHECK(vertex.texture_id == 9);
	}
}

TEST_CASE("a texture resolver drives per-face texture_id and its own merge key") {
	cellulose::World<> world;
	// a 2x1 strip of "grass" on the XZ plane, so the +Y face is a mergeable run
	put(world, 4, 4, 4, 1);
	put(world, 5, 4, 4, 1);
	// and a "dirt" block (id 2) touching in +X, sharing the side texture with grass
	put(world, 6, 4, 4, 2);

	// grass: top=10 side=11 bottom=12 ; dirt: all 11
	const auto texture_of = [](const cellulose::HotCellAttribute &p_attribute, cellulose::i32 p_face) -> cellulose::TextureID {
		if (p_attribute.block_id == 1)
			return p_face == 2 ? 10u : (p_face == 3 ? 12u : 11u);
		return 11u;
	};

	const auto mesh = cellulose::mesh_chunk(world, { 0, 0, 0 }, world_solid(), texture_of);

	// +Y faces: grass tops use layer 10, dirt top uses layer 11
	int grass_top = 0;
	int dirt_top = 0;
	for (const auto &vertex : mesh.vertices) {
		if (!(vertex.normal == cellulose::Vec3{ 0, 1, 0 }))
			continue;
		if (vertex.texture_id == 10)
			++grass_top;
		else if (vertex.texture_id == 11)
			++dirt_top;
	}
	CHECK(grass_top == 4); // the 2x1 grass run merged into one quad
	CHECK(dirt_top == 4); // dirt's own top quad

	// the -Z faces of all three blocks share layer 11 -> one merged quad
	CHECK(quads_facing(mesh, cellulose::Vec3{ 0, 0, -1 }) == 1);
}

TEST_CASE("differing face texture prevents that face from merging") {
	cellulose::World<> world;
	put(world, 1, 1, 1, 1);
	put(world, 2, 1, 1, 2); // distinct block ids

	// every face shares layer 5 -> the whole surface merges as before
	const auto uniform = [](const cellulose::HotCellAttribute &, cellulose::i32) -> cellulose::TextureID {
		return 5u;
	};
	CHECK(quads_facing(
				  cellulose::mesh_chunk(world, { 0, 0, 0 }, world_solid(), uniform),
				  cellulose::Vec3{ 0, 1, 0 }) == 1);

	// +Y layer differs by block id (100 vs 200); the other faces stay on layer 5
	const auto split = [](const cellulose::HotCellAttribute &p_attribute, cellulose::i32 p_face) -> cellulose::TextureID {
		return p_face == 2 ? static_cast<cellulose::TextureID>(p_attribute.block_id * 100) : 5u;
	};
	const auto mesh = cellulose::mesh_chunk(world, { 0, 0, 0 }, world_solid(), split);
	CHECK(quads_facing(mesh, cellulose::Vec3{ 0, 1, 0 }) == 2); // +Y split
	CHECK(quads_facing(mesh, cellulose::Vec3{ 0, -1, 0 }) == 1); // -Y still merged
}

TEST_CASE("mesh_chunk_lod accepts a texture resolver too") {
	cellulose::World<> world;
	put(world, 0, 0, 0, 1);

	const auto texture_of = [](const cellulose::HotCellAttribute &, cellulose::i32) -> cellulose::TextureID {
		return 42u;
	};
	const auto mesh = cellulose::mesh_chunk_lod(world, { 0, 0, 0 }, 1, world_solid(), texture_of);
	CHECK(mesh.vertices.size() == 24);
	for (const auto &vertex : mesh.vertices)
		CHECK(vertex.texture_id == 42);
}
