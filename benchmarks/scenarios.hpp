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
	int chunks = 256;

	bool csv = false;
	bool wide_window = false; //!< widen the seqlock torn-read window with a yield
};

// One entry point per benchmark file. `main.cpp` maps CLI names to these.
auto run_noop(const Config &p_config) -> void; // harness self-test

} //namespace cellbench

#endif // CEL_BENCH_SCENARIOS_HPP
