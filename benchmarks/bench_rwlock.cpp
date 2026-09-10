#include "harness.hpp"
#include "scenarios.hpp"

#include <cellulose/chunk.hpp>

#include <fmt/format.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <utility>
#include <vector>

namespace cellbench {

namespace {

using Big = std::array<cellulose::u8, 64>; // > 8 bytes → sparse tier
using SparseChunk = cellulose::Chunk<cellulose::HotCellAttribute, cellulose::PackedChunkAttributes<>, cellulose::SparseChunkAttributes<Big>>;

constexpr cellulose::size key_space = 2000;
constexpr cellulose::size seeded = 1000;

// A cheap per-thread PRNG (LCG) so the read/write key stream isn't predictable
// but costs nothing.
struct Lcg final {
	u64 state;
	auto next(cellulose::size p_modulo) -> cellulose::size {
		state = state * 6364136223846793005ULL + 1442695040888963407ULL;
		return static_cast<cellulose::size>(state >> 33) % p_modulo;
	}
};

struct RwlockRow final {
	int readers = 0;
	int writers = 0;
	double reads_per_sec = 0.0;
	double writes_per_sec = 0.0;
	u64 read_p99 = 0;
	u64 read_max = 0;
	u64 write_p99 = 0;
};

auto run_config(const Config &p_config, int p_readers, int p_writers) -> RwlockRow {
	SparseChunk chunk;
	chunk.write_sparse([&](auto &p_sparse) {
		auto &map = p_sparse.template get<Big>();
		for (cellulose::size i = 0; i < seeded; ++i)
			map[i] = Big{};
	});

	std::vector<Role> roles;
	if (p_readers > 0)
		roles.push_back(Role{ "reader", p_readers,
				[&](const std::atomic<bool> &p_stop, ThreadContext &p_context) {
					Lcg rng{ 0x9E3779B97F4A7C15ULL + static_cast<u64>(reinterpret_cast<std::uintptr_t>(&p_context)) };
					while (!p_stop.load(std::memory_order_relaxed)) {
						const cellulose::size key = rng.next(key_space);
						const Stopwatch stopwatch;
						const bool present = chunk.read_sparse([&](const auto &p_sparse) {
							const auto &map = p_sparse.template get<Big>();
							const auto iterator = map.find(key);
							if (iterator == map.end())
								return false;
							do_not_optimize(iterator->second);
							return true;
						});
						p_context.record(stopwatch.elapsed_ns());
						do_not_optimize(present);
						p_context.tick();
					}
				} });
	if (p_writers > 0)
		roles.push_back(Role{ "writer", p_writers,
				[&](const std::atomic<bool> &p_stop, ThreadContext &p_context) {
					Lcg rng{ 0xD1B54A32D192ED03ULL + static_cast<u64>(reinterpret_cast<std::uintptr_t>(&p_context)) };
					while (!p_stop.load(std::memory_order_relaxed)) {
						const cellulose::size key = rng.next(key_space);
						const Stopwatch stopwatch;
						chunk.write_sparse([&](auto &p_sparse) {
							auto &map = p_sparse.template get<Big>();
							if (map.contains(key))
								map.erase(key);
							else
								map[key] = Big{};
						});
						p_context.record(stopwatch.elapsed_ns());
						p_context.tick();
					}
				} });

	const Report report = run(p_config.duration, p_config.warmup, roles);

	RwlockRow row;
	row.readers = p_readers;
	row.writers = p_writers;
	for (const RoleResult &role : report.roles) {
		if (role.name == "reader") {
			row.reads_per_sec = role.ops_per_sec;
			row.read_p99 = role.latency.percentile(0.99);
			row.read_max = role.latency.max_ns();
		} else if (role.name == "writer") {
			row.writes_per_sec = role.ops_per_sec;
			row.write_p99 = role.latency.percentile(0.99);
		}
	}
	return row;
}

} //namespace

auto run_rwlock(const Config &p_config) -> void {
	std::vector<std::pair<int, int>> grid;
	if (p_config.single) {
		grid.emplace_back(p_config.readers, p_config.writers);
	} else {
		for (const int readers : { 1, 2, 4, 8 })
			grid.emplace_back(readers, 1);
		for (const int writers : { 0, 2 })
			grid.emplace_back(4, writers);
	}

	if (p_config.csv) {
		fmt::print("scenario,readers,writers,reads_per_sec,writes_per_sec,read_p99_ns,read_max_ns,write_p99_ns\n");
	} else {
		fmt::print("### sparse RWLock — {} ms/config\n", p_config.duration.count());
		fmt::print("{:>7} {:>7} {:>15} {:>15} {:>10} {:>10} {:>10}\n",
				"readers", "writers", "reads/s", "writes/s", "rd-p99", "rd-max", "wr-p99");
	}

	for (const auto [readers, writers] : grid) {
		const RwlockRow row = run_config(p_config, readers, writers);
		if (p_config.csv) {
			fmt::print("rwlock,{},{},{:.0f},{:.0f},{},{},{}\n",
					row.readers, row.writers, row.reads_per_sec, row.writes_per_sec,
					row.read_p99, row.read_max, row.write_p99);
		} else {
			fmt::print("{:>7} {:>7} {:>15.0f} {:>15.0f} {:>8}ns {:>8}ns {:>8}ns\n",
					row.readers, row.writers, row.reads_per_sec, row.writes_per_sec,
					row.read_p99, row.read_max, row.write_p99);
		}
	}
}

} //namespace cellbench
