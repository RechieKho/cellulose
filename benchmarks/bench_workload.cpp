#include "harness.hpp"
#include "scenarios.hpp"

#include <cellulose/collision.hpp>
#include <cellulose/coordinate.hpp>
#include <cellulose/mesh.hpp>
#include <cellulose/raycast.hpp>
#include <cellulose/world.hpp>

#include <fmt/core.h>
#include <array>
#include <atomic>
#include <tuple>
#include <vector>

namespace cellbench {

namespace {

constexpr int scene_chunks = 16; // 16 chunks along +X

struct Lcg final {
	u64 state;
	auto next(u64 p_modulo) -> u64 {
		state = state * 6364136223846793005ULL + 1442695040888963407ULL;
		return (state >> 33) % p_modulo;
	}
	auto next_f64(double p_lo, double p_hi) -> double {
		state = state * 6364136223846793005ULL + 1442695040888963407ULL;
		const double unit = static_cast<double>(state >> 11) / static_cast<double>(1ULL << 53);
		return p_lo + unit * (p_hi - p_lo);
	}
};

auto is_solid(const cellulose::HotCellAttribute &p_attribute) -> bool {
	return p_attribute.block_id != 0;
}

// A ChunkMesh is well-formed: whole quads, indices in range.
auto mesh_ok(const cellulose::ChunkMesh &p_mesh) -> bool {
	if (p_mesh.vertices.size() % 4 != 0)
		return false;
	if (p_mesh.indices.size() != p_mesh.vertices.size() / 4 * 6)
		return false;
	for (const cellulose::u32 index : p_mesh.indices)
		if (index >= p_mesh.vertices.size())
			return false;
	return true;
}

auto seed(cellulose::World<> &p_world) -> void {
	for (cellulose::i32 c = 0; c < scene_chunks; ++c) {
		auto &chunk = p_world.chunk(cellulose::ChunkPosition{ c, 0, 0 });
		for (cellulose::u8 x = 0; x < 32; ++x)
			for (cellulose::u8 z = 0; z < 32; ++z) {
				chunk.hot_attribute(cellulose::LocalPosition{ x, 0, z }).block_id = 1; // floor
				if ((x ^ z) % 3 == 0)
					chunk.hot_attribute(cellulose::LocalPosition{ x, static_cast<cellulose::u8>(1 + (x % 5)), z }).block_id = 2;
			}
	}
}

struct WorkloadRow final {
	int meshers = 0;
	int editors = 0;
	int queriers = 0;
	double meshes_per_sec = 0.0;
	double edits_per_sec = 0.0;
	double queries_per_sec = 0.0;
	u64 mesh_errors = 0;
};

auto run_config(const Config &p_config, int p_meshers, int p_editors, int p_queriers) -> WorkloadRow {
	cellulose::World<> world;
	seed(world);
	std::atomic<u64> mesh_errors{ 0 };

	std::vector<Role> roles;
	if (p_meshers > 0)
		roles.push_back(Role{ "mesher", p_meshers,
				[&](const std::atomic<bool> &p_stop, ThreadContext &p_context) {
					Lcg rng{ 0xA5A5A5A5ULL + static_cast<u64>(p_context.index) };
					while (!p_stop.load(std::memory_order_relaxed)) {
						const cellulose::ChunkPosition position{
							static_cast<cellulose::i32>(rng.next(scene_chunks)), 0, 0
						};
						const auto mesh = cellulose::mesh_chunk(world, position, is_solid);
						if (!mesh_ok(mesh))
							mesh_errors.fetch_add(1, std::memory_order_relaxed);
						do_not_optimize(mesh.vertices.size());
						p_context.tick();
					}
				} });
	if (p_editors > 0)
		roles.push_back(Role{ "editor", p_editors,
				[&](const std::atomic<bool> &p_stop, ThreadContext &p_context) {
					Lcg rng{ 0x1234567ULL + static_cast<u64>(p_context.index) };
					while (!p_stop.load(std::memory_order_relaxed)) {
						const cellulose::ChunkPosition position{
							static_cast<cellulose::i32>(rng.next(scene_chunks)), 0, 0
						};
						const cellulose::CellIndex cell = static_cast<cellulose::CellIndex>(rng.next(cellulose::chunk_cell_count));
						const cellulose::u16 id = static_cast<cellulose::u16>(rng.next(3));
						auto *chunk = world.find_chunk(position);
						if (chunk != nullptr)
							chunk->write_hot([&](auto &p_hot) { p_hot[cell] = cellulose::HotCellAttribute{ id, 0 }; });
						p_context.tick();
					}
				} });
	if (p_queriers > 0)
		roles.push_back(Role{ "querier", p_queriers,
				[&](const std::atomic<bool> &p_stop, ThreadContext &p_context) {
					Lcg rng{ 0xFEEDFACEULL + static_cast<u64>(p_context.index) };
					while (!p_stop.load(std::memory_order_relaxed)) {
						const cellulose::Vec3d origin{
							rng.next_f64(0.0, scene_chunks * 32.0), rng.next_f64(0.0, 32.0), rng.next_f64(0.0, 32.0)
						};
						if (rng.next(2) == 0) {
							const cellulose::Ray ray{ origin, { rng.next_f64(-1, 1), rng.next_f64(-1, 1), rng.next_f64(-1, 1) } };
							const auto hit = cellulose::raycast(world, ray, 64.0, is_solid);
							do_not_optimize(hit.has_value());
						} else {
							const cellulose::Aabb box{ origin, origin + cellulose::Vec3d{ 0.8, 1.8, 0.8 } };
							const auto move = cellulose::move_aabb(world, box, { 0.0, -0.5, 0.0 }, is_solid);
							do_not_optimize(move.collided);
						}
						p_context.tick();
					}
				} });

	const Report report = run(p_config.duration, p_config.warmup, roles);

	WorkloadRow row;
	row.meshers = p_meshers;
	row.editors = p_editors;
	row.queriers = p_queriers;
	for (const RoleResult &role : report.roles) {
		if (role.name == "mesher")
			row.meshes_per_sec = role.ops_per_sec;
		else if (role.name == "editor")
			row.edits_per_sec = role.ops_per_sec;
		else if (role.name == "querier")
			row.queries_per_sec = role.ops_per_sec;
	}
	row.mesh_errors = mesh_errors.load();
	return row;
}

auto emit(const Config &p_config, const WorkloadRow &p_row) -> void {
	if (p_config.csv)
		fmt::print("workload,{},{},{},{:.0f},{:.0f},{:.0f},{}\n",
				p_row.meshers, p_row.editors, p_row.queriers,
				p_row.meshes_per_sec, p_row.edits_per_sec, p_row.queries_per_sec, p_row.mesh_errors);
	else
		fmt::print("{:>7} {:>7} {:>8} {:>12.0f} {:>12.0f} {:>12.0f} {:>12}\n",
				p_row.meshers, p_row.editors, p_row.queriers,
				p_row.meshes_per_sec, p_row.edits_per_sec, p_row.queries_per_sec, p_row.mesh_errors);
}

} //namespace

auto run_workload(const Config &p_config) -> void {
	if (p_config.csv)
		fmt::print("scenario,meshers,editors,queriers,meshes_per_sec,edits_per_sec,queries_per_sec,mesh_errors\n");
	else {
		fmt::print("### mixed workload (meshing while editing) — {} ms/config\n", p_config.duration.count());
		fmt::print("{:>7} {:>7} {:>8} {:>12} {:>12} {:>12} {:>12}\n",
				"meshers", "editors", "queriers", "meshes/s", "edits/s", "queries/s", "mesh-errs");
	}

	if (p_config.single) {
		emit(p_config, run_config(p_config, p_config.meshers, p_config.editors, p_config.queriers));
		return;
	}

	constexpr std::array<std::tuple<int, int, int>, 4> grid{
		std::tuple{ 4, 1, 0 }, std::tuple{ 4, 2, 2 }, std::tuple{ 8, 4, 4 }, std::tuple{ 2, 8, 2 }
	};
	for (const auto [meshers, editors, queriers] : grid)
		emit(p_config, run_config(p_config, meshers, editors, queriers));
}

} //namespace cellbench
