#include "harness.hpp"
#include "scenarios.hpp"

#include <atomic>
#include <vector>

namespace cellbench {

// Proves the harness: two roles that just spin, one of them timing a tiny
// synthetic operation so the latency path is exercised too.
auto run_noop(const Config &p_config) -> void {
	const std::vector<Role> roles{
		Role{ "spin", 2,
				[](const std::atomic<bool> &p_stop, ThreadContext &p_context) {
					u64 accumulator = 0;
					while (!p_stop.load(std::memory_order_relaxed)) {
						accumulator += 1;
						p_context.tick();
					}
					do_not_optimize(accumulator);
				} },
		Role{ "timed-work", 1,
				[](const std::atomic<bool> &p_stop, ThreadContext &p_context) {
					while (!p_stop.load(std::memory_order_relaxed)) {
						const Stopwatch stopwatch;
						volatile int x = 0;
						for (int i = 0; i < 200; ++i)
							x += i;
						p_context.record(stopwatch.elapsed_ns());
						p_context.tick();
					}
				} },
	};

	const Report report = run(p_config.duration, p_config.warmup, roles);
	print_report("noop", report, p_config.csv);
}

} //namespace cellbench
