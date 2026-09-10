#include "harness.hpp"
#include "scenarios.hpp"

#include <fmt/core.h>
#include <cstdlib>
#include <map>
#include <string>
#include <string_view>

namespace {

using cellbench::Config;
using ScenarioFn = void (*)(const Config &);

const std::map<std::string, ScenarioFn> registry{
	{ "noop", cellbench::run_noop },
	{ "seqlock", cellbench::run_seqlock },
	{ "rwlock", cellbench::run_rwlock },
	{ "world", cellbench::run_world },
};

auto require_release() -> void {
#ifndef NDEBUG
	fmt::print(stderr,
			"cellulose_benchmarks was built without NDEBUG — Debug numbers are meaningless.\n"
			"Reconfigure with -DCMAKE_BUILD_TYPE=Release.\n");
	std::exit(1);
#endif
}

auto usage() -> void {
	fmt::print(
			"usage: cellulose_benchmarks [--scenario NAME|all] [options]\n"
			"  --scenario NAME      one of: all");
	for (const auto &[name, fn] : registry)
		fmt::print(", {}", name);
	fmt::print(
			"\n"
			"  --duration-ms N      measurement window (default 3000)\n"
			"  --warmup-ms N        warm-up before measuring (default 200)\n"
			"  --runs N             runs per config (default 5; scenarios report the median)\n"
			"  --readers/--writers/--queriers/--meshers/--editors/--streamers N\n"
			"  --chunks N           working-set size where a scenario uses one\n"
			"  --single             run one config from the flags, not the default sweep\n"
			"  --wide-window        widen the seqlock torn-read window\n"
			"  --csv                machine-readable output (scenarios print their own header)\n");
}

} //namespace

auto main(int p_argc, char **p_argv) -> int {
	require_release();

	Config config;
	std::string scenario = "all";

	const auto value = [&](int &p_i) -> const char * {
		if (p_i + 1 >= p_argc) {
			fmt::print(stderr, "missing value for {}\n", p_argv[p_i]);
			std::exit(2);
		}
		return p_argv[++p_i];
	};

	for (int i = 1; i < p_argc; ++i) {
		const std::string_view argument = p_argv[i];
		if (argument == "--scenario")
			scenario = value(i);
		else if (argument == "--duration-ms")
			config.duration = std::chrono::milliseconds(std::atoi(value(i)));
		else if (argument == "--warmup-ms")
			config.warmup = std::chrono::milliseconds(std::atoi(value(i)));
		else if (argument == "--runs")
			config.runs = std::atoi(value(i));
		else if (argument == "--readers")
			config.readers = std::atoi(value(i));
		else if (argument == "--writers")
			config.writers = std::atoi(value(i));
		else if (argument == "--queriers")
			config.queriers = std::atoi(value(i));
		else if (argument == "--meshers")
			config.meshers = std::atoi(value(i));
		else if (argument == "--editors")
			config.editors = std::atoi(value(i));
		else if (argument == "--streamers")
			config.streamers = std::atoi(value(i));
		else if (argument == "--chunks")
			config.chunks = std::atoi(value(i));
		else if (argument == "--wide-window")
			config.wide_window = true;
		else if (argument == "--single")
			config.single = true;
		else if (argument == "--csv")
			config.csv = true;
		else if (argument == "--help" || argument == "-h") {
			usage();
			return 0;
		} else {
			fmt::print(stderr, "unknown argument: {}\n", argument);
			return 2;
		}
	}

	if (scenario == "all") {
		for (const auto &[name, function] : registry)
			function(config);
		return 0;
	}

	const auto entry = registry.find(scenario);
	if (entry == registry.end()) {
		fmt::print(stderr, "no scenario named '{}'\n", scenario);
		return 2;
	}
	entry->second(config);
	return 0;
}
