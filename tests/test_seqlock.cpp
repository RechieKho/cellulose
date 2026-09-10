#include <doctest/doctest.h>

#include <cellulose/seqlock.hpp>

#include <atomic>
#include <thread>
#include <utility>

TEST_CASE("seqlock round-trips a value and advances the sequence by two per write") {
	cellulose::SeqLock lock;
	CHECK(lock.sequence() == 0);

	struct Point final {
		cellulose::u64 x;
		cellulose::u64 y;
	};
	Point point{ 0, 0 };

	lock.write([&] {
		point.x = 3;
		point.y = 4;
	});
	CHECK(lock.sequence() == 2);

	const auto sum = lock.read([&] { return point.x + point.y; });
	CHECK(sum == 7);

	lock.write([&] { point.x = 10; });
	CHECK(lock.sequence() == 4);

	// A void read functor is also supported.
	cellulose::u64 seen = 0;
	lock.read([&] { seen = point.x + point.y; });
	CHECK(seen == 14);
}

// The payload is two atomics so this micro-test is itself free of data races;
// what is under test is the retry loop rejecting torn snapshots.
TEST_CASE("seqlock readers never observe a torn write under contention") {
	cellulose::SeqLock lock;
	std::atomic<cellulose::u64> first{ 0 };
	std::atomic<cellulose::u64> second{ 0 };

	constexpr cellulose::u64 iterations = 200'000;
	std::atomic<bool> start{ false };

	std::thread writer([&] {
		while (!start.load(std::memory_order_acquire)) {
		}
		for (cellulose::u64 i = 1; i <= iterations; ++i) {
			lock.write([&] {
				first.store(i, std::memory_order_relaxed);
				std::this_thread::yield(); // widen the torn-read window
				second.store(i, std::memory_order_relaxed);
			});
		}
	});

	const auto run_reader = [&] {
		while (!start.load(std::memory_order_acquire)) {
		}
		cellulose::u64 mismatches = 0;
		for (cellulose::u64 i = 0; i < iterations; ++i) {
			const auto pair = lock.read([&] {
				return std::pair<cellulose::u64, cellulose::u64>{
					first.load(std::memory_order_relaxed),
					second.load(std::memory_order_relaxed)
				};
			});
			if (pair.first != pair.second)
				++mismatches;
		}
		return mismatches;
	};

	std::atomic<cellulose::u64> reader_a_mismatches{ 0 };
	std::atomic<cellulose::u64> reader_b_mismatches{ 0 };
	std::thread reader_a([&] { reader_a_mismatches.store(run_reader()); });
	std::thread reader_b([&] { reader_b_mismatches.store(run_reader()); });

	start.store(true, std::memory_order_release);
	writer.join();
	reader_a.join();
	reader_b.join();

	CHECK(reader_a_mismatches.load() == 0);
	CHECK(reader_b_mismatches.load() == 0);
}
