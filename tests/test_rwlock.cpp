#include <doctest/doctest.h>

#include <cellulose/rwlock.hpp>
#include <cellulose/types.hpp>

#include <ankerl/unordered_dense.h>
#include <atomic>
#include <thread>

TEST_CASE("rwlock read observes what write stored, and forwards results") {
	cellulose::RWLock lock;
	cellulose::u64 value = 0;

	lock.write([&] { value = 42; });
	const auto seen = lock.read([&] { return value; });
	CHECK(seen == 42);

	const auto doubled = lock.write([&] { value *= 2; return value; });
	CHECK(doubled == 84);
}

// A growing map reallocates; without the exclusive lock a concurrent reader
// iterating it would touch freed storage. With the lock, every reader snapshot
// is internally consistent.
TEST_CASE("rwlock serialises a growing map against concurrent readers") {
	cellulose::RWLock lock;
	ankerl::unordered_dense::map<cellulose::u64, cellulose::u64> map;

	constexpr cellulose::u64 count = 3'000;
	std::atomic<bool> start{ false };
	std::atomic<cellulose::u64> inconsistencies{ 0 };

	std::thread writer([&] {
		while (!start.load(std::memory_order_acquire)) {
		}
		for (cellulose::u64 i = 0; i < count; ++i)
			lock.write([&] { map.emplace(i, i * 2); });
	});

	const auto run_reader = [&] {
		while (!start.load(std::memory_order_acquire)) {
		}
		for (cellulose::u64 i = 0; i < count; ++i) {
			const bool consistent = lock.read([&] {
				for (const auto &[key, mapped] : map)
					if (mapped != key * 2)
						return false;
				return true;
			});
			if (!consistent)
				++inconsistencies;
		}
	};

	std::thread reader_a(run_reader);
	std::thread reader_b(run_reader);

	start.store(true, std::memory_order_release);
	writer.join();
	reader_a.join();
	reader_b.join();

	CHECK(inconsistencies.load() == 0);
	CHECK(map.size() == count);
}
