#include <doctest/doctest.h>

#include <cellulose/mesh.hpp>

#include <vector>

namespace {

using cellulose::i32;
using cellulose::u16;

struct Grid final {
	i32 size;
	std::vector<cellulose::MeshSample> samples;

	explicit Grid(i32 p_size) :
			size(p_size),
			samples(static_cast<std::size_t>((p_size + 2) * (p_size + 2) * (p_size + 2))) {}

	auto at(i32 p_x, i32 p_y, i32 p_z) -> cellulose::MeshSample & {
		const i32 stride = size + 2;
		return samples[static_cast<std::size_t>(((p_x + 1) * stride + (p_y + 1)) * stride + (p_z + 1))];
	}

	auto set_solid(i32 p_x, i32 p_y, i32 p_z, u16 p_block_id = 1) -> void {
		auto &cell = at(p_x, p_y, p_z);
		cell.solid = true;
		cell.block_id = p_block_id;
	}

	auto mesh() -> cellulose::ChunkMesh { return cellulose::greedy_mesh(samples, size, 1.0f); }
};

auto quads_facing(const cellulose::ChunkMesh &p_mesh, const cellulose::Vec3 &p_normal) -> int {
	int vertices = 0;
	for (const auto &vertex : p_mesh.vertices)
		if (vertex.normal == p_normal)
			++vertices;
	return vertices / 4;
}

} //namespace

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
		CHECK(vertex.position.y >= 3.0f);
		CHECK(vertex.position.z <= 4.0f);
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
	CHECK(mesh.indices.size() == 36);

	bool has_far_corner = false;
	for (const auto &vertex : mesh.vertices)
		if (vertex.position == cellulose::Vec3{ 2, 2, 2 })
			has_far_corner = true;
	CHECK(has_far_corner);
}

TEST_CASE("an interior shared face is culled") {
	Grid grid(8);
	grid.set_solid(3, 3, 3);
	grid.set_solid(4, 3, 3);

	const auto mesh = grid.mesh();
	// +X face of cell 3 (at x = 4) is hidden; only cell 4's +X face (at x = 5) survives.
	CHECK(quads_facing(mesh, cellulose::Vec3{ 1, 0, 0 }) == 1);
	CHECK(quads_facing(mesh, cellulose::Vec3{ -1, 0, 0 }) == 1);
	// two-wide box: top / bottom / front / back each merge to one 2x1 quad.
	CHECK(mesh.vertices.size() == 24);
}

TEST_CASE("differing face brightness prevents that face from merging") {
	Grid grid(4);
	grid.set_solid(1, 1, 1);
	grid.set_solid(2, 1, 1);
	grid.at(1, 1, 1).brightness[2] = 1; // +Y face
	grid.at(2, 1, 1).brightness[2] = 3;

	const auto mesh = grid.mesh();
	CHECK(quads_facing(mesh, cellulose::Vec3{ 0, 1, 0 }) == 2); // +Y split
	CHECK(quads_facing(mesh, cellulose::Vec3{ 0, -1, 0 }) == 1); // -Y still merged
	CHECK(mesh.vertices.size() == 28); // 7 quads
}

TEST_CASE("a +X quad is wound so its triangles face +X") {
	Grid grid(8);
	grid.set_solid(3, 3, 3);
	const auto mesh = grid.mesh();

	// find the +X quad's first triangle
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
