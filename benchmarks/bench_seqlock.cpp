#include "harness.hpp"
#include "scenarios.hpp"

#include <cellulose/chunk.hpp>
#include <cellulose/coordinate.hpp>
#include <cellulose/morton.hpp>

#include <fmt/core.h>
#include <atomic>
#include <thread>
#include <utility>
#include <vector>

namespace cellbench {

namespace {

struct SeqlockRow final {
	int readers = 0;
	int writers = 0;
	double reads_per_sec = 0.0;
	double writes_per_sec = 0.0;
	u64 write_p50 = 0;
	u64 write_p99 = 0;
	double retry_rate = 0.0;
	u64 violations = 0;
};

// Writer keeps block_id == state; a reader that ever sees them differ has
// observed a torn read the seqlock failed to reject.
auto run_config(const Config &p_config, int p_readers, int p_writers) -> SeqlockRow {
	cellulose::Chunk<> chunk;
	const cellulose::CellIndex index =
			cellulose::encode_cell_index(cellulose::LocalPosition{ 8, 8, 8 });

	chunk.write_hot([&](auto &p_hot) {
#ifdef CELLULOSE_STRICT_ATOMICS
		p_hot[index] = cellulose::HotCellAttribute{ 1, 1 };
#else
		p_hot[index].block_id = 1;
		p_hot[index].state = 1;
#endif
	});

	std::atomic<u64> violations{ 0 };

	std::vector<Role> roles;
	if (p_readers > 0)
		roles.push_back(Role{ "reader", p_readers,
				[&](const std::atomic<bool> &p_stop, ThreadContext &p_context) {
					while (!p_stop.load(std::memory_order_relaxed)) {
						const auto cell = chunk.read_hot(
								[&](const auto &p_hot) { return p_hot[index]; });
						if (cell.block_id != cell.state)
							violations.fetch_add(1, std::memory_order_relaxed);
						do_not_optimize(cell);
						p_context.tick();
					}
				} });
	if (p_writers > 0)
		roles.push_back(Role{ "writer", p_writers,
				[&](const std::atomic<bool> &p_stop, ThreadContext &p_context) {
					cellulose::u16 v = 2;
					while (!p_stop.load(std::memory_order_relaxed)) {
						const Stopwatch stopwatch;
						chunk.write_hot([&](auto &p_hot) {
#ifdef CELLULOSE_STRICT_ATOMICS
							p_hot[index] = cellulose::HotCellAttribute{ v, v };
#else
							p_hot[index].block_id = v;
							if (p_config.wide_window)
								std::this_thread::yield();
							p_hot[index].state = v;
#endif
						});
						p_context.record(stopwatch.elapsed_ns());
						p_context.tick();
						if (++v == 0)
							v = 2;
					}
				} });

	const u64 retries_before = chunk.hot_retry_count();
	const Report report = run(p_config.duration, p_config.warmup, roles);
	const u64 retries_after = chunk.hot_retry_count();

	SeqlockRow row;
	row.readers = p_readers;
	row.writers = p_writers;
	u64 reader_ops = 0;
	for (const RoleResult &role : report.roles) {
		if (role.name == "reader") {
			row.reads_per_sec = role.ops_per_sec;
			reader_ops = role.ops;
		} else if (role.name == "writer") {
			row.writes_per_sec = role.ops_per_sec;
			row.write_p50 = role.latency.percentile(0.5);
			row.write_p99 = role.latency.percentile(0.99);
		}
	}
	row.retry_rate = reader_ops > 0
			? static_cast<double>(retries_after - retries_before) / static_cast<double>(reader_ops)
			: 0.0;
	row.violations = violations.load();
	return row;
}

auto mode_label() -> const char * {
#ifdef CELLULOSE_STRICT_ATOMICS
	return "strict-atomics";
#else
	return "default";
#endif
}

} //namespace

auto run_seqlock(const Config &p_config) -> void {
	std::vector<std::pair<int, int>> grid;
	if (p_config.single) {
		grid.emplace_back(p_config.readers, p_config.writers);
	} else {
		for (const int readers : { 1, 2, 4, 8 })
			grid.emplace_back(readers, 1);
		for (const int writers : { 0, 2, 4 })
			grid.emplace_back(4, writers);
	}

	if (p_config.csv) {
		fmt::print("scenario,mode,readers,writers,reads_per_sec,writes_per_sec,write_p50_ns,write_p99_ns,retry_rate,violations\n");
	} else {
		fmt::print("### seqlock hot tier — {} — {} ms/config\n", mode_label(), p_config.duration.count());
		fmt::print("{:>7} {:>7} {:>15} {:>15} {:>9} {:>9} {:>10} {:>10}\n",
				"readers", "writers", "reads/s", "writes/s", "wr-p50", "wr-p99", "retry", "violations");
	}

	for (const auto [readers, writers] : grid) {
		const SeqlockRow row = run_config(p_config, readers, writers);
		if (p_config.csv) {
			fmt::print("seqlock,{},{},{},{:.0f},{:.0f},{},{},{:.5f},{}\n",
					mode_label(), row.readers, row.writers, row.reads_per_sec, row.writes_per_sec,
					row.write_p50, row.write_p99, row.retry_rate, row.violations);
		} else {
			fmt::print("{:>7} {:>7} {:>15.0f} {:>15.0f} {:>7}ns {:>7}ns {:>9.4f} {:>10}\n",
					row.readers, row.writers, row.reads_per_sec, row.writes_per_sec,
					row.write_p50, row.write_p99, row.retry_rate, row.violations);
		}
	}
}

} //namespace cellbench
