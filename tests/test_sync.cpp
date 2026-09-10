#include <doctest/doctest.h>

#include <cellulose/sync.hpp>

#include <cstddef>
#include <mutex>
#include <utility>

static_assert(cellulose::cache_line_size >= 64);
static_assert(alignof(cellulose::Padded<int>) >= cellulose::cache_line_size);
static_assert(sizeof(cellulose::Padded<int>) >= cellulose::cache_line_size);

TEST_CASE("Padded exposes its wrapped value") {
	cellulose::Padded<int> value{ 7 };
	CHECK(*value == 7);

	*value = 9;
	CHECK(value.value == 9);

	cellulose::Padded<std::pair<int, int>> pair{ { 1, 2 } };
	CHECK(pair->first == 1);
	CHECK(pair->second == 2);
}

TEST_CASE("Padded default-constructs its value") {
	cellulose::Padded<std::mutex> lock;
	CHECK(lock->try_lock());
	lock->unlock();
}

TEST_CASE("adjacent Padded members never share a cache line") {
	struct Pair final {
		cellulose::Padded<int> a;
		cellulose::Padded<int> b;
	};

	Pair pair;
	const auto distance =
			reinterpret_cast<std::byte *>(&pair.b) - reinterpret_cast<std::byte *>(&pair.a);
	CHECK(distance >= static_cast<std::ptrdiff_t>(cellulose::cache_line_size));
}
