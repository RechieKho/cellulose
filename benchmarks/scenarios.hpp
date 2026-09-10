#ifndef CEL_BENCH_SCENARIOS_HPP
#define CEL_BENCH_SCENARIOS_HPP

#include <chrono>

namespace cellbench {

/// @brief Knobs shared by every scenario; set from the CLI.
struct Config final {
	std::chrono::milliseconds duration{ 3000 };
	std::chrono::milliseconds warmup{ 200 };
	int runs = 5;

	int readers = 4;
	int writers = 1;
	int queriers = 0;
	int meshers = 0;
	int editors = 0;
	int streamers = 0;
	int chunks = 1024;

	bool csv = false;
	bool single = false; //!< run one config from the flags above, not the default sweep
	bool wide_window = false; //!< widen the seqlock torn-read window with a yield
};

// One entry point per benchmark file. `main.cpp` maps CLI names to these.
auto run_noop(const Config &p_config) -> void; // harness self-test
auto run_seqlock(const Config &p_config) -> void; // B1, B2
auto run_rwlock(const Config &p_config) -> void; // B3
auto run_world(const Config &p_config) -> void; // B4, B5
auto run_cursor(const Config &p_config) -> void; // B6
auto run_workload(const Config &p_config) -> void; // B7

} //namespace cellbench

#endif // CEL_BENCH_SCENARIOS_HPP
