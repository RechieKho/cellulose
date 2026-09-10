#include "harness.hpp"
#include "scenarios.hpp"

#include <cellulose/coordinate.hpp>
#include <cellulose/world.hpp>

#include <fmt/core.h>
#include <atomic>
#include <cstdint>
#include <string_view>
#include <vector>

namespace cellbench {

namespace {

// A 1-byte hot cell so each chunk is ~32 KB — the directory, not chunk memory,
// is what B4/B5 measure.
struct MicroHot final {
	cellulose::u8 block_id = 0;
};
static_assert(cellulose::HotAttribute<MicroHot>);

using MicroChunk = cellulose::Chunk<MicroHot>;

struct Lcg final {
	u64 state;
	auto next(u64 p_modulo) -> u64 {
		state = state * 6364136223846793005ULL + 1442695040888963407ULL;
		return (state >> 33) % p_modulo;
	}
};

struct WorldRow final {
	std::string_view mode;
	int chunks = 0;
	int queriers = 0;
	int streamers = 0;
	double lookups_per_sec = 0.0;
	double dir_writes_per_sec = 0.0;
	u64 lookup_p99 = 0;
	u64 lookup_max = 0;
};

template <cellulose::ChunkStorage Storage>
auto run_config(const Config &p_config, int p_queriers, int p_streamers) -> WorldRow {
	const auto chunk_count = static_cast<cellulose::i32>(p_config.chunks);
	cellulose::World<MicroChunk, Storage> world;
	for (cellulose::i32 i = 0; i < chunk_count; ++i)
		world.chunk(cellulose::ChunkPosition{ i, 0, 0 });

	std::vector<Role> roles;
	roles.push_back(Role{ "querier", p_queriers,
			[&](const std::atomic<bool> &p_stop, ThreadContext &p_context) {
				Lcg rng{ 0x9E3779B97F4A7C15ULL + static_cast<u64>(p_context.index) };
				while (!p_stop.load(std::memory_order_relaxed)) {
					const cellulose::ChunkPosition position{
						static_cast<cellulose::i32>(rng.next(static_cast<u64>(chunk_count))), 0, 0
					};
					const Stopwatch stopwatch;
					auto handle = world.find_chunk(position);
					p_context.record(stopwatch.elapsed_ns());
					do_not_optimize(handle);
					p_context.tick();
				}
			} });
	if (p_streamers > 0)
		roles.push_back(Role{ "streamer", p_streamers,
				[&](const std::atomic<bool> &p_stop, ThreadContext &p_context) {
					// disjoint band per streamer, clear of the seeded [0, chunk_count) range
					const cellulose::i32 base = 1'000'000 + p_context.index * 100'000;
					cellulose::i32 cursor = base;
					while (!p_stop.load(std::memory_order_relaxed)) {
						world.chunk(cellulose::ChunkPosition{ cursor, 0, 0 });
						if (cursor - base >= 64)
							world.remove_chunk(cellulose::ChunkPosition{ cursor - 64, 0, 0 });
						++cursor;
						p_context.tick();
					}
				} });

	const Report report = run(p_config.duration, p_config.warmup, roles);

	WorldRow row;
	row.mode = (Storage == cellulose::ChunkStorage::Shared) ? "shared" : "unique";
	row.chunks = p_config.chunks;
	row.queriers = p_queriers;
	row.streamers = p_streamers;
	for (const RoleResult &role : report.roles) {
		if (role.name == "querier") {
			row.lookups_per_sec = role.ops_per_sec;
			row.lookup_p99 = role.latency.percentile(0.99);
			row.lookup_max = role.latency.max_ns();
		} else if (role.name == "streamer") {
			row.dir_writes_per_sec = role.ops_per_sec;
		}
	}
	return row;
}

auto emit(const Config &p_config, const WorldRow &p_row) -> void {
	if (p_config.csv) {
		fmt::print("world,{},{},{},{},{:.0f},{:.0f},{},{}\n",
				p_row.mode, p_row.chunks, p_row.queriers, p_row.streamers,
				p_row.lookups_per_sec, p_row.dir_writes_per_sec, p_row.lookup_p99, p_row.lookup_max);
	} else {
		fmt::print("{:>7} {:>7} {:>9} {:>10} {:>15.0f} {:>14.0f} {:>9}ns {:>9}ns\n",
				p_row.mode, p_row.chunks, p_row.queriers, p_row.streamers,
				p_row.lookups_per_sec, p_row.dir_writes_per_sec, p_row.lookup_p99, p_row.lookup_max);
	}
}

} //namespace

auto run_world(const Config &p_config) -> void {
	if (p_config.csv)
		fmt::print("scenario,mode,chunks,queriers,streamers,lookups_per_sec,dir_writes_per_sec,lookup_p99_ns,lookup_max_ns\n");
	else {
		fmt::print("### world directory — {} chunks — {} ms/config\n", p_config.chunks, p_config.duration.count());
		fmt::print("{:>7} {:>7} {:>9} {:>10} {:>15} {:>14} {:>11} {:>11}\n",
				"mode", "chunks", "queriers", "streamers", "lookups/s", "dir-writes/s", "lu-p99", "lu-max");
	}

	if (p_config.single) {
		emit(p_config, run_config<cellulose::ChunkStorage::Unique>(p_config, p_config.queriers, p_config.streamers));
		return;
	}

	for (const int queriers : { 1, 4, 8, 16 }) // B4/B5: pure lookup scaling, both policies
		emit(p_config, run_config<cellulose::ChunkStorage::Unique>(p_config, queriers, 0));
	for (const int queriers : { 1, 4, 8, 16 })
		emit(p_config, run_config<cellulose::ChunkStorage::Shared>(p_config, queriers, 0));
	for (const int streamers : { 1, 4 }) // B4: lookup vs directory churn
		emit(p_config, run_config<cellulose::ChunkStorage::Unique>(p_config, 8, streamers));
}

} //namespace cellbench
