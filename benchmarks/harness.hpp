#ifndef CEL_BENCH_HARNESS_HPP
#define CEL_BENCH_HARNESS_HPP

// Dependency-free micro-harness for asymmetric concurrent benchmarks: several
// "roles", each with its own thread count and loop body, run together for a
// fixed wall-clock window after a warm-up. Reports per-role throughput and a
// coarse (power-of-two bucketed) latency histogram for roles that opt in to
// timing their operation.

#include <fmt/core.h>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace cellbench {

using u64 = std::uint64_t;

/// @brief Stops the optimiser from eliding a value the benchmark computed.
template <typename T>
inline void do_not_optimize(const T &p_value) {
#if defined(_MSC_VER)
	const volatile char *p = reinterpret_cast<const volatile char *>(&p_value);
	(void)*p;
	std::atomic_signal_fence(std::memory_order_acq_rel);
#else
	asm volatile("" : : "r,m"(p_value) : "memory");
#endif
}

/// @brief Power-of-two-bucketed latency histogram (buckets are 2^b .. 2^(b+1) ns).
class Histogram final {
	static constexpr int bucket_count = 48;
	std::array<u64, bucket_count> m_counts{};

	static auto bucket_of(u64 p_ns) -> int {
		const int b = p_ns == 0 ? 0 : std::bit_width(p_ns) - 1;
		return b >= bucket_count ? bucket_count - 1 : b;
	}
	static auto upper_bound(int p_bucket) -> u64 {
		return p_bucket >= 63 ? ~u64{ 0 } : (u64{ 1 } << (p_bucket + 1));
	}

public:
	auto add(u64 p_ns) -> void { ++m_counts[static_cast<std::size_t>(bucket_of(p_ns))]; }

	auto merge(const Histogram &p_other) -> void {
		for (int i = 0; i < bucket_count; ++i)
			m_counts[static_cast<std::size_t>(i)] += p_other.m_counts[static_cast<std::size_t>(i)];
	}

	auto total() const -> u64 {
		u64 sum = 0;
		for (const u64 c : m_counts)
			sum += c;
		return sum;
	}

	/// @brief Upper bound (ns) of the bucket holding the `p_fraction` quantile.
	auto percentile(double p_fraction) const -> u64 {
		const u64 n = total();
		if (n == 0)
			return 0;
		const u64 target = static_cast<u64>(p_fraction * static_cast<double>(n));
		u64 cumulative = 0;
		for (int b = 0; b < bucket_count; ++b) {
			cumulative += m_counts[static_cast<std::size_t>(b)];
			if (cumulative > target)
				return upper_bound(b);
		}
		return upper_bound(bucket_count - 1);
	}

	auto max_ns() const -> u64 {
		for (int b = bucket_count - 1; b >= 0; --b)
			if (m_counts[static_cast<std::size_t>(b)] != 0)
				return upper_bound(b);
		return 0;
	}
};

/// @brief Per-thread state handed to a role body.
struct ThreadContext final {
	std::atomic<u64> ops{ 0 };
	Histogram latency; //!< thread-owned; only read after the thread joins
	const std::atomic<bool> *measuring = nullptr;

	auto tick() -> void { ops.fetch_add(1, std::memory_order_relaxed); }
	auto record(u64 p_ns) -> void {
		if (measuring->load(std::memory_order_relaxed))
			latency.add(p_ns);
	}
};

/// @brief One participant in a benchmark: `p_threads` threads all running `body`
/// in a loop until `stop` is set.
struct Role final {
	std::string name;
	int threads = 1;
	std::function<void(const std::atomic<bool> &stop, ThreadContext &context)> body;
};

struct RoleResult final {
	std::string name;
	int threads = 0;
	u64 ops = 0;
	double ops_per_sec = 0.0;
	Histogram latency;
};

struct Report final {
	std::vector<RoleResult> roles;
	double seconds = 0.0;
};

/// @brief Run every role's threads together for `p_duration` (after `p_warmup`).
inline auto run(std::chrono::milliseconds p_duration, std::chrono::milliseconds p_warmup, const std::vector<Role> &p_roles) -> Report {
	std::deque<ThreadContext> contexts; // stable addresses; ThreadContext is non-movable
	std::vector<std::pair<int, int>> role_span; // [begin, end) into `contexts`
	int total_threads = 0;
	for (const Role &role : p_roles) {
		role_span.emplace_back(total_threads, total_threads + role.threads);
		total_threads += role.threads;
	}
	for (int i = 0; i < total_threads; ++i)
		contexts.emplace_back();

	std::atomic<bool> stop{ false };
	std::atomic<bool> measuring{ false };

	std::vector<std::thread> threads;
	threads.reserve(static_cast<std::size_t>(total_threads));
	for (std::size_t r = 0; r < p_roles.size(); ++r) {
		const auto [begin, end] = role_span[r];
		for (int t = begin; t < end; ++t) {
			ThreadContext &context = contexts[static_cast<std::size_t>(t)];
			context.measuring = &measuring;
			threads.emplace_back([&, body = p_roles[r].body] { body(stop, context); });
		}
	}

	std::this_thread::sleep_for(p_warmup);
	std::vector<u64> warm_ops(static_cast<std::size_t>(total_threads));
	for (int t = 0; t < total_threads; ++t)
		warm_ops[static_cast<std::size_t>(t)] = contexts[static_cast<std::size_t>(t)].ops.load(std::memory_order_relaxed);

	const auto start = std::chrono::steady_clock::now();
	measuring.store(true, std::memory_order_relaxed);
	std::this_thread::sleep_for(p_duration);
	stop.store(true, std::memory_order_relaxed);
	const auto end = std::chrono::steady_clock::now();

	for (std::thread &thread : threads)
		thread.join();

	const double seconds = std::chrono::duration<double>(end - start).count();

	Report report;
	report.seconds = seconds;
	for (std::size_t r = 0; r < p_roles.size(); ++r) {
		const auto [begin, span_end] = role_span[r];
		RoleResult result;
		result.name = p_roles[r].name;
		result.threads = p_roles[r].threads;
		for (int t = begin; t < span_end; ++t) {
			result.ops += contexts[static_cast<std::size_t>(t)].ops.load(std::memory_order_relaxed) -
					warm_ops[static_cast<std::size_t>(t)];
			result.latency.merge(contexts[static_cast<std::size_t>(t)].latency);
		}
		result.ops_per_sec = seconds > 0.0 ? static_cast<double>(result.ops) / seconds : 0.0;
		report.roles.push_back(std::move(result));
	}
	return report;
}

/// @brief Print a report: one human line per role, or CSV rows when `p_csv`.
inline auto print_report(std::string_view p_scenario, const Report &p_report, bool p_csv) -> void {
	if (!p_csv)
		fmt::print("=== {} ({:.2f}s) ===\n", p_scenario, p_report.seconds);
	for (const RoleResult &role : p_report.roles) {
		if (p_csv) {
			fmt::print("{},{},{},{},{:.0f},{},{},{}\n",
					p_scenario, role.name, role.threads, role.ops, role.ops_per_sec,
					role.latency.percentile(0.5), role.latency.percentile(0.99), role.latency.max_ns());
		} else {
			fmt::print("  {:<18} x{:<2} {:>15.0f} ops/s", role.name, role.threads, role.ops_per_sec);
			if (role.latency.total() > 0)
				fmt::print("   p50={}ns p99={}ns max={}ns",
						role.latency.percentile(0.5), role.latency.percentile(0.99), role.latency.max_ns());
			fmt::print("\n");
		}
	}
}

/// @brief A steady_clock stopwatch for timing a single operation.
class Stopwatch final {
	std::chrono::steady_clock::time_point m_start = std::chrono::steady_clock::now();

public:
	auto elapsed_ns() const -> u64 {
		return static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - m_start)
						.count());
	}
};

} //namespace cellbench

#endif // CEL_BENCH_HARNESS_HPP
