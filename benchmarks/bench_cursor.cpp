#include "harness.hpp"
#include "scenarios.hpp"

#include <cellulose/coordinate.hpp>
#include <cellulose/cursor.hpp>
#include <cellulose/morton.hpp>
#include <cellulose/world.hpp>

#include <fmt/core.h>
#include <atomic>
#include <vector>

namespace cellbench {

namespace {

constexpr cellulose::i64 walk_length = 2048; // cells along +X → ~64 chunks

auto expected_block_id(cellulose::i64 p_x) -> cellulose::BlockID {
	return static_cast<cellulose::BlockID>((p_x & 0xff) | 1);
}

template <typename WorldType>
auto seed(WorldType &p_world) -> void {
	for (cellulose::i64 x = 0; x < walk_length; ++x) {
		const cellulose::WorldPosition cell{ x, 5, 5 };
		p_world.chunk(cellulose::to_chunk_position(cell))
				.hot_attribute(cellulose::to_local_position(cell))
				.block_id = expected_block_id(x);
	}
}

struct CursorRow final {
	const char *variant = "";
	int walkers = 0;
	double cells_per_sec = 0.0;
	u64 errors = 0;
};

auto run_variant(const Config &p_config, const char *p_variant, int p_walkers, bool p_use_cursor) -> CursorRow {
	cellulose::World<> world;
	seed(world);
	std::atomic<u64> errors{ 0 };

	const std::vector<Role> roles{
		Role{ "walker", p_walkers,
				[&](const std::atomic<bool> &p_stop, ThreadContext &p_context) {
					while (!p_stop.load(std::memory_order_relaxed)) {
						if (p_use_cursor) {
							cellulose::impl::ChunkCursor cursor(world);
							for (cellulose::i64 x = 0; x < walk_length; ++x) {
								const auto snapshot = cursor.hot(cellulose::WorldPosition{ x, 5, 5 });
								if (snapshot.has_value() && snapshot->block_id != expected_block_id(x))
									errors.fetch_add(1, std::memory_order_relaxed);
								do_not_optimize(snapshot);
								p_context.tick();
							}
						} else {
							for (cellulose::i64 x = 0; x < walk_length; ++x) {
								const cellulose::WorldPosition cell{ x, 5, 5 };
								auto *chunk = world.find_chunk(cellulose::to_chunk_position(cell));
								if (chunk != nullptr) {
									const cellulose::LocalPosition local = cellulose::to_local_position(cell);
									const auto snapshot = chunk->read_hot([&](const auto &p_hot) {
										return p_hot[cellulose::encode_cell_index(local)];
									});
									if (snapshot.block_id != expected_block_id(x))
										errors.fetch_add(1, std::memory_order_relaxed);
									do_not_optimize(snapshot);
								}
								p_context.tick();
							}
						}
					}
				} },
	};

	const Report report = run(p_config.duration, p_config.warmup, roles);
	CursorRow row;
	row.variant = p_variant;
	row.walkers = p_walkers;
	row.cells_per_sec = report.roles.front().ops_per_sec;
	row.errors = errors.load();
	return row;
}

// A cursor walk on a Shared world while streamers churn a disjoint band — the
// walk's cached pointers must stay valid and its reads correct.
auto run_concurrent_unload(const Config &p_config) -> CursorRow {
	cellulose::World<cellulose::Chunk<>, cellulose::ChunkStorage::Shared> world;
	seed(world);
	std::atomic<u64> errors{ 0 };

	const std::vector<Role> roles{
		Role{ "walker", 1,
				[&](const std::atomic<bool> &p_stop, ThreadContext &p_context) {
					while (!p_stop.load(std::memory_order_relaxed)) {
						cellulose::impl::ChunkCursor cursor(world);
						for (cellulose::i64 x = 0; x < walk_length; ++x) {
							const auto snapshot = cursor.hot(cellulose::WorldPosition{ x, 5, 5 });
							if (snapshot.has_value() && snapshot->block_id != expected_block_id(x))
								errors.fetch_add(1, std::memory_order_relaxed);
							do_not_optimize(snapshot);
							p_context.tick();
						}
					}
				} },
		Role{ "streamer", 2,
				[&](const std::atomic<bool> &p_stop, ThreadContext &p_context) {
					const cellulose::i32 base = 1'000'000 + p_context.index * 100'000;
					cellulose::i32 cursor = base;
					while (!p_stop.load(std::memory_order_relaxed)) {
						world.chunk(cellulose::ChunkPosition{ cursor, 0, 0 });
						if (cursor - base >= 64)
							world.remove_chunk(cellulose::ChunkPosition{ cursor - 64, 0, 0 });
						++cursor;
						p_context.tick();
					}
				} },
	};

	const Report report = run(p_config.duration, p_config.warmup, roles);
	CursorRow row;
	row.variant = "cursor + concurrent unload";
	row.walkers = 1;
	row.cells_per_sec = report.roles.front().ops_per_sec;
	row.errors = errors.load();
	return row;
}

auto emit(const Config &p_config, const CursorRow &p_row) -> void {
	if (p_config.csv)
		fmt::print("cursor,{},{},{:.0f},{}\n", p_row.variant, p_row.walkers, p_row.cells_per_sec, p_row.errors);
	else
		fmt::print("  {:<28} x{:<2} {:>15.0f} cells/s   errors={}\n",
				p_row.variant, p_row.walkers, p_row.cells_per_sec, p_row.errors);
}

} //namespace

auto run_cursor(const Config &p_config) -> void {
	if (p_config.csv)
		fmt::print("scenario,variant,walkers,cells_per_sec,errors\n");
	else
		fmt::print("### ChunkCursor — {} ms/config\n", p_config.duration.count());

	for (const int walkers : { 1, 4 }) {
		emit(p_config, run_variant(p_config, "cursor", walkers, true));
		emit(p_config, run_variant(p_config, "find_chunk per cell", walkers, false));
	}
	emit(p_config, run_concurrent_unload(p_config));
}

} //namespace cellbench
