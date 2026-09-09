# Cellulose Foundation (Chunk + World) Implementation Plan

> **Status: COMPLETE / ARCHIVED.** All 8 tasks were implemented on branch
> `worktree-feat-foundation-chunk-world` (commits `33cf989..166f51f`). This file is
> vendored as the historical record of the decisions behind the foundation code.
> The as-built code and `cellulose_tests` suite realize the step-by-step content
> below; see `2026-09-08-foundation-chunk-world-ledger.md` for the execution ledger
> (pre-flight conflict scan, rulings R1–R5, deferred concerns C1/C2) and
> `../../ARCHITECTURE_SPEC.md` + `../../REMAINING_TASKS.md` for the self-contained
> current-state docs.

**Goal:** Stand up a testable core data structure for `cellulose` — a `Chunk` (Morton-coded per-cell storage) held in a `World` (Robin-Hood hash table keyed by chunk coordinate) — plus a doctest harness, integrating the existing `HotCellAttribute`, `PackedCellAttributeCollection`, `SparseCellAttributeCollection`, and `BlockRegistry` primitives.

**Architecture:** Header-only additions under `inc/cellulose/`. Coordinate value types and conversions in `coordinate.hpp`; a thin libmorton wrapper in `morton.hpp`; `Chunk` in `chunk.hpp` (fixed-size hot `std::array` + optional packed/sparse attribute collections); `World` in `world.hpp` (`ankerl::unordered_dense::map<ChunkPosition, ChunkType, ChunkPositionHash>`). A new `tests/` tree builds one `cellulose_tests` binary via doctest, wired into CTest.

**Tech Stack:** C++20, CMake (>= 3.10), header-only INTERFACE library `libcellulose`; deps already vendored/fetched: `libmorton`, `ankerl::unordered_dense`, `fmt`, `raylib`. New test dep: `doctest` v2.4.11 via `FetchContent`.

**Spec:** `README.md` (§ "Core Data Structure"). No detailed spec doc exists; this plan treats the README as the spec and records every concrete parameter it invents under *Assumptions* below. Correct any assumption before/while executing.

## Context

`cellulose` aims to be a performant concurrent C++ voxel library. Today the repo only has leaf primitives: `HotCellAttribute` / the two `*CellAttributeCollection` templates (`inc/cellulose/cell.hpp`) and a `Block` / `BlockRegistry` / builders (`inc/cellulose/block.hpp`), plus `types.hpp` and `inspect.hpp`. There is **no chunk, no world container, and no test framework** — so nothing that actually stores voxels and nothing is verified automatically.

This plan builds that missing foundation and the harness to test it. The three later subsystems from the README — concurrency/thread-safety, spatial querying, rendering/meshing — will each get their own plan built on top of the `Chunk`/`World` types produced here. Keeping this plan foundation-only means every later plan starts from working, tested storage.

## Global Constraints

- **C++ standard:** C++20 (`set(CMAKE_CXX_STANDARD 20)` already in `CMakeLists.txt`). Arithmetic right-shift of signed integers is well-defined in C++20 — the coordinate math relies on it.
- **Header-only:** `libcellulose` is an `INTERFACE` target. All new code lives in headers under `inc/cellulose/`. No new `.cpp` in `src/` except edits to the existing `src/main.cpp` demo.
- **Namespacing / style:** follow the existing pattern exactly — classes with logic go in `namespace cellulose::impl` as `template <typename = void>` (or already-parameterised) classes, then are surfaced via `using` / alias templates in `namespace cellulose`. Public data members have **no** `m_` prefix; private members keep `m_`. Parameters are `p_`-prefixed. Trailing-return-type (`auto f() -> T`) everywhere. Tabs for indent (`.clang-format` enforces; run it on touched files).
- **Include guards:** `CEL_<NAME>_HPP`.
- **Naming/copy:** commit messages end with the two attribution trailers configured for this session (`Co-Authored-By:` + `Claude-Session:`).
- **Frequent commits:** one commit per task minimum, at each "Commit" step.

## Assumptions (invented here — verify before executing)

| # | Parameter | Value chosen | Rationale |
|---|-----------|--------------|-----------|
| A1 | Chunk edge length | `32` (⇒ `32³ = 32768` cells) | Common Minecraft-style chunk section size; Morton code fits in 15 bits ⇒ `u32` index with room to spare. |
| A2 | Cell index type | `CellIndex = u32`, produced by `libmorton::morton3D_32_encode` | 15 bits needed; `u32` is libmorton's smallest 3D-encode output. |
| A3 | Local coordinate type | `LocalPosition { u8 x, y, z; }`, each in `[0, 32)` | 1 byte per axis is plenty for 0–31. |
| A4 | Chunk-grid coordinate | `ChunkPosition { i32 x, y, z; }` | Signed, ±2³¹ chunks per axis ≫ any practical world. `sizeof == 12`, no padding. |
| A5 | Absolute block coordinate | `WorldPosition { i64 x, y, z; }` | Signed 64-bit avoids overflow when combining chunk*32 + local. |
| A6 | World↔chunk split | `chunk = world >> 5`, `local = world & 31` (arithmetic shift/mask) | Exact floor-division/modulo for power-of-two edge length, negatives included. |
| A7 | Chunk hot storage | `std::array<HotCellAttribute, 32768>` (AoS, Morton-ordered) | Matches README "Hot Data … AoS … Morton-coded 1D array". |
| A8 | Chunk cold/freezing storage | `Chunk` is templated on a `PackedCellAttributeCollection` type and a `SparseCellAttributeCollection` type, both defaulting to the **empty** collection | No concrete cold/freezing attribute type exists yet; later plans (lighting, tile entities) supply them. Defaults keep `Chunk<>` zero-overhead. |
| A9 | `World` chunk creation | `world.chunk(pos)` is get-**or-create** (default-constructs a `Chunk`); `find_chunk(pos)` is lookup-only returning `ChunkType*` / `nullptr` | Mirrors `std::unordered_map::operator[]` vs `find`; explicit names avoid the footgun. |
| A10 | `ChunkPosition` hashing | standalone `ChunkPositionHash` functor, `using is_avalanching = void;`, delegates to `ankerl::unordered_dense::detail::wyhash::hash(&p, sizeof(p))` | `unordered_dense` requires an avalanching hash; hashing the 12 contiguous padding-free bytes is simplest and deterministic. |
| A11 | Test framework | `doctest` v2.4.11 via `FetchContent`; single aggregated `cellulose_tests` binary; `doctest_discover_tests` registers cases with CTest | Single-header, fastest compile, minimal wiring — good fit for a header-only lib. Chosen by the user. |
| A12 | Test build toggle | `option(CELLULOSE_BUILD_TESTS ... )` defaulting ON only when `cellulose` is the top-level project | Consumers embedding `cellulose` via `add_subdirectory` should not pay for its tests. |

---

## File Structure

**Created:**
- `inc/cellulose/coordinate.hpp` — `LocalPosition`, `ChunkPosition`, `WorldPosition` value types; `ChunkPositionHash`; free conversion functions. One responsibility: coordinate representation + conversion.
- `inc/cellulose/morton.hpp` — `encode_cell_index(LocalPosition) -> CellIndex`, `decode_cell_index(CellIndex) -> LocalPosition`. One responsibility: Morton (de)coding of in-chunk positions.
- `inc/cellulose/chunk.hpp` — `impl::Chunk<PackedCollection, SparseCollection>` + `chunk_edge_length` / `chunk_cell_count` constants + `cellulose::Chunk` alias template. One responsibility: per-chunk voxel storage.
- `inc/cellulose/world.hpp` — `impl::World<ChunkType>` + `cellulose::World` alias template. One responsibility: the chunk container + world-coordinate access.
- `inc/cellulose/cellulose.hpp` — umbrella header including all public headers. Convenience only.
- `tests/CMakeLists.txt` — fetches doctest, builds `cellulose_tests`, `doctest_discover_tests`.
- `tests/main.cpp` — `#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN` + `#include <doctest/doctest.h>` (the only translation unit that defines the doctest main).
- `tests/test_cell.cpp` — tests for the existing `cell.hpp` primitives (collection `get()` + `HotCellAttribute` state round-trips).
- `tests/test_coordinate.cpp` — coordinate conversion tests.
- `tests/test_morton.cpp` — Morton round-trip tests.
- `tests/test_chunk.cpp` — `Chunk` accessor tests.
- `tests/test_world.cpp` — `World` container tests.

**Modified:**
- `inc/cellulose/cell.hpp` — fix `*CellAttributeCollection::get()` to return references; fix the 6 face-brightness getter precedence bugs.
- `CMakeLists.txt` — add `CELLULOSE_BUILD_TESTS` option + `enable_testing()` + `add_subdirectory(tests)`.
- `src/main.cpp` — small stdout demo exercising `World` before `InitWindow`.
- `README.md` — short "Implementation status / assumptions" note pointing at this plan's assumption table.

---

## Task 1: doctest harness wired into CTest

**Files:**
- Create: `tests/CMakeLists.txt`, `tests/main.cpp`, `tests/test_smoke.cpp` (temporary, deleted in Task 2)
- Modify: `CMakeLists.txt` (append test wiring after the `add_executable(${EXECUTABLE_NAME} ...)` block, before the final `get_directory_property(HAS_PARENT ...)` block)

**Interfaces:**
- Consumes: nothing.
- Produces: a `cellulose_tests` CMake target and a working `ctest` invocation. Test files are globbed (`tests/*.cpp`), so later tasks just drop in `test_*.cpp` files.

- [ ] **Step 1: Write the failing test**

`tests/test_smoke.cpp`:

```cpp
#include <doctest/doctest.h>

TEST_CASE("doctest harness runs") {
	CHECK(1 + 1 == 2);
}
```

`tests/main.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
```

- [ ] **Step 2: Run to verify it fails (no build yet)**

Run: `cmake -S . -B build && cmake --build build --target cellulose_tests`
Expected: FAIL — target `cellulose_tests` does not exist.

- [ ] **Step 3: Add the test wiring**

`tests/CMakeLists.txt`:

```cmake
include(FetchContent)

FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.11
)
FetchContent_MakeAvailable(doctest)

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable(cellulose_tests ${TEST_SOURCES})
target_link_libraries(cellulose_tests PRIVATE libcellulose doctest::doctest)

list(APPEND CMAKE_MODULE_PATH "${doctest_SOURCE_DIR}/scripts/cmake")
include(doctest)
doctest_discover_tests(cellulose_tests)
```

Append to `CMakeLists.txt` (immediately after the `target_link_libraries(${EXECUTABLE_NAME} ...)` block, around line 92):

```cmake
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(CELLULOSE_BUILD_TESTS_DEFAULT ON)
else()
    set(CELLULOSE_BUILD_TESTS_DEFAULT OFF)
endif()
option(CELLULOSE_BUILD_TESTS "Build cellulose tests" ${CELLULOSE_BUILD_TESTS_DEFAULT})

if(CELLULOSE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake -S . -B build && cmake --build build --target cellulose_tests && ctest --test-dir build --output-on-failure`
Expected: PASS — `doctest harness runs` green, `ctest` exit 0.

- [ ] **Step 5: Commit**

```bash
git add tests CMakeLists.txt
git commit -m "Add doctest harness wired into CTest."
```

---

## Task 2: Fix `*CellAttributeCollection::get()` to return references

**Files:**
- Modify: `inc/cellulose/cell.hpp:187-192` (`PackedCellAttributeCollection::get`), `inc/cellulose/cell.hpp:208-212` (`SparseCellAttributeCollection::get`)
- Create: `tests/test_cell.cpp`
- Delete: `tests/test_smoke.cpp`

**Interfaces:**
- Consumes: `cellulose::PackedCellAttributeCollection<CellCount, Attributes...>`, `cellulose::SparseCellAttributeCollection<Attributes...>`.
- Produces (new signatures later tasks rely on):
  - `PackedCellAttributeCollection::get<Attribute>() -> std::array<Attribute, CellCount>&` (+ `const` overload returning `const std::array<Attribute, CellCount>&`)
  - `SparseCellAttributeCollection::get<Attribute>() -> ankerl::unordered_dense::map<size, Attribute>&` (+ `const` overload)

- [ ] **Step 1: Write the failing test**

`tests/test_cell.cpp`:

```cpp
#include <doctest/doctest.h>

#include <cellulose/cell.hpp>

TEST_CASE("PackedCellAttributeCollection::get returns a mutable reference") {
	cellulose::PackedCellAttributeCollection<4, cellulose::u32> collection;
	collection.get<cellulose::u32>()[2] = 42u;
	CHECK(collection.get<cellulose::u32>()[2] == 42u);
}

TEST_CASE("SparseCellAttributeCollection::get returns a mutable reference") {
	cellulose::SparseCellAttributeCollection<std::array<cellulose::u8, 16>> collection;
	collection.get<std::array<cellulose::u8, 16>>()[7] = std::array<cellulose::u8, 16>{};
	CHECK(collection.get<std::array<cellulose::u8, 16>>().contains(7));
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target cellulose_tests && ctest --test-dir build -R CellAttributeCollection --output-on-failure`
Expected: FAIL — first `CHECK` reads `0`, because `get()` currently returns a **copy**; the write lands in a temporary.

- [ ] **Step 3: Fix the implementations**

`inc/cellulose/cell.hpp` — `PackedCellAttributeCollection`:

```cpp
	template <typename Attribute>
	inline auto get() -> std::array<Attribute, CellCount> & {
		return std::get<std::array<Attribute, CellCount>>(m_attributes);
	}

	template <typename Attribute>
	inline auto get() const -> const std::array<Attribute, CellCount> & {
		return std::get<std::array<Attribute, CellCount>>(m_attributes);
	}
```

`inc/cellulose/cell.hpp` — `SparseCellAttributeCollection`:

```cpp
	template <typename Attribute>
	inline auto get() -> ankerl::unordered_dense::map<size, Attribute> & {
		return std::get<ankerl::unordered_dense::map<size, Attribute>>(m_attributes);
	}

	template <typename Attribute>
	inline auto get() const -> const ankerl::unordered_dense::map<size, Attribute> & {
		return std::get<ankerl::unordered_dense::map<size, Attribute>>(m_attributes);
	}
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build --target cellulose_tests && ctest --test-dir build -R CellAttributeCollection --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Delete the smoke test and re-run the full suite**

```bash
git rm tests/test_smoke.cpp
```

Run: `cmake -S . -B build && cmake --build build --target cellulose_tests && ctest --test-dir build --output-on-failure`
Expected: PASS — suite still green without the smoke placeholder.

- [ ] **Step 6: Commit**

```bash
git add inc/cellulose/cell.hpp tests/test_cell.cpp tests/test_smoke.cpp
git commit -m "Return references from `PackedCellAttributeCollection` and `SparseCellAttributeCollection` getters."
```

---

## Task 3: Fix `HotCellAttribute` face-brightness getter precedence

**Files:**
- Modify: `inc/cellulose/cell.hpp:108-166` (the six `get_*_face_brightness` methods)
- Modify: `tests/test_cell.cpp` (append)

**Interfaces:**
- Consumes: `cellulose::HotCellAttribute` (public members `block_id`, `state`; enums `Pitch`, `Yaw`; getters/setters for pitch, yaw, and 6 face brightnesses).
- Produces: unchanged signatures; `get_<face>_face_brightness()` now returns the 2-bit value shifted down to `[0, 3]`, so `set_X(v); get_X() == v` holds for `v ∈ {0,1,2,3}`.

**Background:** each getter currently reads
`state & (two_bit_mask << OFFSET) >> OFFSET`. `<<` and `>>` share precedence and bind left-to-right, tighter than `&`, so this parses as `state & ((two_bit_mask << OFFSET) >> OFFSET)` = `state & two_bit_mask` — the offset is cancelled out and every face returns the low 2 bits of `state`. The fix is one pair of parentheses around the mask-and.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_cell.cpp`:

```cpp
TEST_CASE("HotCellAttribute face brightness round-trips per face") {
	cellulose::HotCellAttribute attribute{0, 0};

	attribute.set_right_face_brightness(3);
	attribute.set_left_face_brightness(2);
	attribute.set_top_face_brightness(1);
	attribute.set_bottom_face_brightness(3);
	attribute.set_back_face_brightness(2);
	attribute.set_front_face_brightness(1);

	CHECK(attribute.get_right_face_brightness() == 3);
	CHECK(attribute.get_left_face_brightness() == 2);
	CHECK(attribute.get_top_face_brightness() == 1);
	CHECK(attribute.get_bottom_face_brightness() == 3);
	CHECK(attribute.get_back_face_brightness() == 2);
	CHECK(attribute.get_front_face_brightness() == 1);
}

TEST_CASE("HotCellAttribute pitch and yaw round-trip") {
	cellulose::HotCellAttribute attribute{0, 0};

	attribute.set_pitch(cellulose::HotCellAttribute::PITCH_DOWN);
	attribute.set_yaw(cellulose::HotCellAttribute::YAW_LEFT);

	CHECK(attribute.get_pitch() == cellulose::HotCellAttribute::PITCH_DOWN);
	CHECK(attribute.get_yaw() == cellulose::HotCellAttribute::YAW_LEFT);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target cellulose_tests && ctest --test-dir build -R "HotCellAttribute" --output-on-failure`
Expected: FAIL — the "face brightness round-trips per face" case: several faces read back the wrong value (they all return the low 2 bits of `state`). The pitch/yaw case should already pass.

- [ ] **Step 3: Fix the six getters**

For each of `right`, `left`, `top`, `bottom`, `back`, `front`, wrap the mask-and in parentheses. Example (`right`):

```cpp
	constexpr auto get_right_face_brightness() const -> u8 {
		return (state & (two_bit_mask << right_face_brightness_bit_mask_offset)) >> right_face_brightness_bit_mask_offset;
	}
```

Apply the identical parenthesisation to the other five getters (`left_face_brightness_bit_mask_offset`, `top_…`, `bottom_…`, `back_…`, `front_…`).

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build --target cellulose_tests && ctest --test-dir build -R "HotCellAttribute" --output-on-failure`
Expected: PASS — both cases green.

- [ ] **Step 5: Commit**

```bash
git add inc/cellulose/cell.hpp tests/test_cell.cpp
git commit -m "Fix operator precedence in `HotCellAttribute` face-brightness getters."
```

---

## Task 4: Coordinate value types and conversions

**Files:**
- Create: `inc/cellulose/coordinate.hpp`
- Create: `tests/test_coordinate.cpp`

**Interfaces:**
- Consumes: `cellulose::u8`, `cellulose::i32`, `cellulose::i64`, `cellulose::u64`, `cellulose::size` from `types.hpp`; `ankerl::unordered_dense::detail::wyhash::hash`.
- Produces (later tasks rely on these exact names/types):
  - `struct cellulose::LocalPosition { u8 x, y, z; };` with `friend auto operator==(const LocalPosition&, const LocalPosition&) -> bool = default;`
  - `struct cellulose::ChunkPosition { i32 x, y, z; };` with defaulted `operator==`. `static_assert(sizeof(ChunkPosition) == 12)`.
  - `struct cellulose::WorldPosition { i64 x, y, z; };` with defaulted `operator==`.
  - `struct cellulose::ChunkPositionHash { using is_avalanching = void; auto operator()(const ChunkPosition&) const noexcept -> u64; };`
  - `auto cellulose::to_chunk_position(const WorldPosition&) -> ChunkPosition;`
  - `auto cellulose::to_local_position(const WorldPosition&) -> LocalPosition;`
  - `auto cellulose::to_world_position(const ChunkPosition&, const LocalPosition&) -> WorldPosition;`
  - constants live in `chunk.hpp` (Task 6), **not** here — this header must not depend on `chunk.hpp`. Use the literal `5` (shift) / `31` (mask) with an explanatory comment.

- [ ] **Step 1: Write the failing test**

`tests/test_coordinate.cpp`:

```cpp
#include <doctest/doctest.h>

#include <cellulose/coordinate.hpp>

using cellulose::ChunkPosition;
using cellulose::LocalPosition;
using cellulose::WorldPosition;

TEST_CASE("world position splits into chunk + local for the positive octant") {
	const WorldPosition world{35, 2, 63};
	CHECK(cellulose::to_chunk_position(world) == ChunkPosition{1, 0, 1});
	CHECK(cellulose::to_local_position(world) == LocalPosition{3, 2, 31});
}

TEST_CASE("world position splits correctly for negative coordinates") {
	const WorldPosition world{-1, -32, -33};
	CHECK(cellulose::to_chunk_position(world) == ChunkPosition{-1, -1, -2});
	CHECK(cellulose::to_local_position(world) == LocalPosition{31, 0, 31});
}

TEST_CASE("chunk + local recombine into the original world position") {
	const WorldPosition world{-33, 100, 7};
	const auto chunk = cellulose::to_chunk_position(world);
	const auto local = cellulose::to_local_position(world);
	CHECK(cellulose::to_world_position(chunk, local) == world);
}

TEST_CASE("ChunkPositionHash is deterministic and distinguishes neighbours") {
	const cellulose::ChunkPositionHash hash;
	CHECK(hash(ChunkPosition{1, 2, 3}) == hash(ChunkPosition{1, 2, 3}));
	CHECK(hash(ChunkPosition{1, 2, 3}) != hash(ChunkPosition{1, 2, 4}));
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake -S . -B build && cmake --build build --target cellulose_tests`
Expected: FAIL — `cellulose/coordinate.hpp` not found.

- [ ] **Step 3: Write the implementation**

`inc/cellulose/coordinate.hpp`:

```cpp
#ifndef CEL_COORDINATE_HPP
#define CEL_COORDINATE_HPP

#include "types.hpp"
#include <ankerl/unordered_dense.h>

namespace cellulose {

/// @brief Position of a cell within a chunk. Each axis is in `[0, chunk_edge_length)`.
struct LocalPosition final {
	u8 x;
	u8 y;
	u8 z;

	friend auto operator==(const LocalPosition &, const LocalPosition &) -> bool = default;
};

/// @brief Position of a chunk in the chunk grid.
struct ChunkPosition final {
	i32 x;
	i32 y;
	i32 z;

	friend auto operator==(const ChunkPosition &, const ChunkPosition &) -> bool = default;
};
static_assert(
		sizeof(ChunkPosition) == 12,
		"`ChunkPosition` must be padding-free so it can be hashed by raw bytes.");

/// @brief Absolute block position in the world.
struct WorldPosition final {
	i64 x;
	i64 y;
	i64 z;

	friend auto operator==(const WorldPosition &, const WorldPosition &) -> bool = default;
};

/// @brief Avalanching hash for `ChunkPosition`, required by `ankerl::unordered_dense::map`.
struct ChunkPositionHash final {
	using is_avalanching = void;

	auto operator()(const ChunkPosition &p_position) const noexcept -> u64 {
		return ankerl::unordered_dense::detail::wyhash::hash(&p_position, sizeof(p_position));
	}
};

// `chunk_edge_length` is 32 (see `chunk.hpp`); dividing/mod by a power of two is an
// arithmetic shift by 5 / mask with 31, which floors correctly for negative operands in C++20.
inline auto to_chunk_position(const WorldPosition &p_world) -> ChunkPosition {
	return ChunkPosition{
		static_cast<i32>(p_world.x >> 5),
		static_cast<i32>(p_world.y >> 5),
		static_cast<i32>(p_world.z >> 5)
	};
}

inline auto to_local_position(const WorldPosition &p_world) -> LocalPosition {
	return LocalPosition{
		static_cast<u8>(p_world.x & 31),
		static_cast<u8>(p_world.y & 31),
		static_cast<u8>(p_world.z & 31)
	};
}

inline auto to_world_position(const ChunkPosition &p_chunk, const LocalPosition &p_local) -> WorldPosition {
	return WorldPosition{
		(static_cast<i64>(p_chunk.x) << 5) | p_local.x,
		(static_cast<i64>(p_chunk.y) << 5) | p_local.y,
		(static_cast<i64>(p_chunk.z) << 5) | p_local.z
	};
}

} //namespace cellulose

#endif // CEL_COORDINATE_HPP
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build --target cellulose_tests && ctest --test-dir build -R "world position|chunk \+ local|ChunkPositionHash" --output-on-failure`
Expected: PASS — all four cases green.

- [ ] **Step 5: Commit**

```bash
git add inc/cellulose/coordinate.hpp tests/test_coordinate.cpp
git commit -m "Add coordinate value types and world/chunk/local conversions."
```

---

## Task 5: Morton cell-index (de)coding

**Files:**
- Create: `inc/cellulose/morton.hpp`
- Create: `tests/test_morton.cpp`

**Interfaces:**
- Consumes: `cellulose::LocalPosition` (from `coordinate.hpp`); `libmorton::morton3D_32_encode(uint_fast16_t, uint_fast16_t, uint_fast16_t)` / `libmorton::morton3D_32_decode(uint_fast32_t, uint_fast16_t&, uint_fast16_t&, uint_fast16_t&)` from `<libmorton/morton.h>`.
- Produces:
  - `using cellulose::CellIndex = u32;`
  - `auto cellulose::encode_cell_index(const LocalPosition&) -> CellIndex;`
  - `auto cellulose::decode_cell_index(CellIndex) -> LocalPosition;`

- [ ] **Step 1: Write the failing test**

`tests/test_morton.cpp`:

```cpp
#include <doctest/doctest.h>

#include <cellulose/morton.hpp>

TEST_CASE("cell index round-trips for every position in a 32^3 chunk") {
	bool seen[32u * 32u * 32u] = {};

	for (cellulose::u8 x = 0; x < 32; ++x) {
		for (cellulose::u8 y = 0; y < 32; ++y) {
			for (cellulose::u8 z = 0; z < 32; ++z) {
				const cellulose::LocalPosition position{x, y, z};
				const auto index = cellulose::encode_cell_index(position);

				REQUIRE(index < 32u * 32u * 32u);
				CHECK_FALSE(seen[index]); // encoding is a bijection
				seen[index] = true;

				CHECK(cellulose::decode_cell_index(index) == position);
			}
		}
	}
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake -S . -B build && cmake --build build --target cellulose_tests`
Expected: FAIL — `cellulose/morton.hpp` not found.

- [ ] **Step 3: Write the implementation**

`inc/cellulose/morton.hpp`:

```cpp
#ifndef CEL_MORTON_HPP
#define CEL_MORTON_HPP

#include "coordinate.hpp"
#include "types.hpp"
#include <libmorton/morton.h>
#include <cstdint>

namespace cellulose {

/// @brief Morton-encoded index of a cell within a chunk's 1D storage arrays.
using CellIndex = u32;

/// @brief Interleave a local position into its Morton code.
inline auto encode_cell_index(const LocalPosition &p_position) -> CellIndex {
	return static_cast<CellIndex>(libmorton::morton3D_32_encode(
			static_cast<uint_fast16_t>(p_position.x),
			static_cast<uint_fast16_t>(p_position.y),
			static_cast<uint_fast16_t>(p_position.z)));
}

/// @brief De-interleave a Morton code back into a local position.
inline auto decode_cell_index(CellIndex p_index) -> LocalPosition {
	uint_fast16_t x = 0;
	uint_fast16_t y = 0;
	uint_fast16_t z = 0;
	libmorton::morton3D_32_decode(static_cast<uint_fast32_t>(p_index), x, y, z);
	return LocalPosition{
		static_cast<u8>(x),
		static_cast<u8>(y),
		static_cast<u8>(z)
	};
}

} //namespace cellulose

#endif // CEL_MORTON_HPP
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build --target cellulose_tests && ctest --test-dir build -R "cell index round-trips" --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add inc/cellulose/morton.hpp tests/test_morton.cpp
git commit -m "Add Morton cell-index encode/decode helpers."
```

---

## Task 6: `Chunk`

**Files:**
- Create: `inc/cellulose/chunk.hpp`
- Create: `tests/test_chunk.cpp`

**Interfaces:**
- Consumes: `cellulose::HotCellAttribute`, `cellulose::PackedCellAttributeCollection`, `cellulose::SparseCellAttributeCollection` (from `cell.hpp`); `cellulose::LocalPosition` (from `coordinate.hpp`); `cellulose::CellIndex`, `cellulose::encode_cell_index`, `cellulose::decode_cell_index` (from `morton.hpp`); `cellulose::size` (from `types.hpp`).
- Produces:
  - `inline constexpr size cellulose::chunk_edge_length = 32;`
  - `inline constexpr size cellulose::chunk_cell_count = chunk_edge_length * chunk_edge_length * chunk_edge_length;` (= 32768)
  - alias template `cellulose::Chunk<PackedCollection = PackedCellAttributeCollection<chunk_cell_count>, SparseCollection = SparseCellAttributeCollection<>>` over `impl::Chunk<...>`, a `final` class with:
    - `auto hot_attribute(const LocalPosition&) -> HotCellAttribute&` (+ `const` overload)
    - `auto hot_attribute(CellIndex) -> HotCellAttribute&` (+ `const` overload)
    - `auto fill_hot(const HotCellAttribute&) -> void`
    - `auto packed() -> PackedCollection&` (+ `const` overload)
    - `auto sparse() -> SparseCollection&` (+ `const` overload)
  - `impl::Chunk` is default-constructible (zero-initialised hot array).

- [ ] **Step 1: Write the failing test**

`tests/test_chunk.cpp`:

```cpp
#include <doctest/doctest.h>

#include <cellulose/chunk.hpp>

TEST_CASE("chunk edge length and cell count are consistent") {
	CHECK(cellulose::chunk_cell_count == cellulose::chunk_edge_length * cellulose::chunk_edge_length * cellulose::chunk_edge_length);
	CHECK(cellulose::chunk_cell_count == 32768u);
}

TEST_CASE("hot attributes are addressable by local position and by cell index") {
	cellulose::Chunk<> chunk;

	const cellulose::LocalPosition position{5, 6, 7};
	chunk.hot_attribute(position).block_id = 9;

	const auto index = cellulose::encode_cell_index(position);
	CHECK(chunk.hot_attribute(index).block_id == 9);

	// A different cell is unaffected.
	CHECK(chunk.hot_attribute(cellulose::LocalPosition{7, 6, 5}).block_id == 0);
}

TEST_CASE("fill_hot writes every cell") {
	cellulose::Chunk<> chunk;
	cellulose::HotCellAttribute stone{42, 0};
	chunk.fill_hot(stone);

	CHECK(chunk.hot_attribute(cellulose::LocalPosition{0, 0, 0}).block_id == 42);
	CHECK(chunk.hot_attribute(cellulose::LocalPosition{31, 31, 31}).block_id == 42);
}

TEST_CASE("chunk exposes its packed and sparse collections") {
	cellulose::Chunk<
			cellulose::PackedCellAttributeCollection<cellulose::chunk_cell_count, cellulose::u16>,
			cellulose::SparseCellAttributeCollection<std::array<cellulose::u8, 16>>>
			chunk;

	chunk.packed().get<cellulose::u16>()[3] = 7;
	CHECK(chunk.packed().get<cellulose::u16>()[3] == 7);
	CHECK(chunk.sparse().get<std::array<cellulose::u8, 16>>().empty());
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake -S . -B build && cmake --build build --target cellulose_tests`
Expected: FAIL — `cellulose/chunk.hpp` not found.

- [ ] **Step 3: Write the implementation**

`inc/cellulose/chunk.hpp`:

```cpp
#ifndef CEL_CHUNK_HPP
#define CEL_CHUNK_HPP

#include "cell.hpp"
#include "coordinate.hpp"
#include "morton.hpp"
#include "types.hpp"
#include <array>

namespace cellulose {

/// @brief Number of cells along one axis of a chunk.
inline constexpr size chunk_edge_length = 32;

/// @brief Total number of cells in a chunk.
inline constexpr size chunk_cell_count = chunk_edge_length * chunk_edge_length * chunk_edge_length;

namespace impl {

/// @brief Storage for one cube of `chunk_edge_length^3` cells.
///
/// Hot attributes are stored inline as a Morton-ordered array of structures for maximum
/// density per cache line. Cold ("packed") and freezing-cold ("sparse") attributes are
/// held in the caller-selected collection types; both default to empty collections until
/// a later subsystem introduces concrete attribute types.
template <
		typename PackedCollection = PackedCellAttributeCollection<chunk_cell_count>,
		typename SparseCollection = SparseCellAttributeCollection<>>
class Chunk final {
public:
	using HotStorage = std::array<HotCellAttribute, chunk_cell_count>;

private:
	HotStorage m_hot{};
	PackedCollection m_packed{};
	SparseCollection m_sparse{};

public:
	auto hot_attribute(CellIndex p_index) -> HotCellAttribute & {
		return m_hot[p_index];
	}

	auto hot_attribute(CellIndex p_index) const -> const HotCellAttribute & {
		return m_hot[p_index];
	}

	auto hot_attribute(const LocalPosition &p_position) -> HotCellAttribute & {
		return m_hot[encode_cell_index(p_position)];
	}

	auto hot_attribute(const LocalPosition &p_position) const -> const HotCellAttribute & {
		return m_hot[encode_cell_index(p_position)];
	}

	auto fill_hot(const HotCellAttribute &p_value) -> void {
		m_hot.fill(p_value);
	}

	auto packed() -> PackedCollection & { return m_packed; }
	auto packed() const -> const PackedCollection & { return m_packed; }

	auto sparse() -> SparseCollection & { return m_sparse; }
	auto sparse() const -> const SparseCollection & { return m_sparse; }
};

} //namespace impl

template <
		typename PackedCollection = PackedCellAttributeCollection<chunk_cell_count>,
		typename SparseCollection = SparseCellAttributeCollection<>>
using Chunk = impl::Chunk<PackedCollection, SparseCollection>;

} //namespace cellulose

#endif // CEL_CHUNK_HPP
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build --target cellulose_tests && ctest --test-dir build -R "chunk|hot attributes|fill_hot" --output-on-failure`
Expected: PASS — all four cases green.

- [ ] **Step 5: Commit**

```bash
git add inc/cellulose/chunk.hpp tests/test_chunk.cpp
git commit -m "Add `Chunk` with Morton-ordered hot storage and attribute collections."
```

---

## Task 7: `World`

**Files:**
- Create: `inc/cellulose/world.hpp`
- Create: `tests/test_world.cpp`

**Interfaces:**
- Consumes: `cellulose::Chunk` (from `chunk.hpp`); `cellulose::ChunkPosition`, `cellulose::ChunkPositionHash`, `cellulose::WorldPosition`, `cellulose::to_chunk_position`, `cellulose::to_local_position` (from `coordinate.hpp`); `cellulose::HotCellAttribute` (from `cell.hpp`); `cellulose::size` (from `types.hpp`); `ankerl::unordered_dense::map`.
- Produces: alias template `cellulose::World<ChunkType = Chunk<>>` over `impl::World<ChunkType>`, a `final` class with:
  - `auto has_chunk(const ChunkPosition&) const -> bool`
  - `auto find_chunk(const ChunkPosition&) -> ChunkType*` (+ `const` overload → `const ChunkType*`); `nullptr` when absent
  - `auto chunk(const ChunkPosition&) -> ChunkType&` — get-or-create (default-constructs)
  - `auto remove_chunk(const ChunkPosition&) -> bool` — `true` if a chunk was erased
  - `auto chunk_count() const -> size`
  - `template <typename Visitor> auto for_each_chunk(Visitor&&) -> void` — invokes `visitor(const ChunkPosition&, ChunkType&)`
  - `auto find_hot_attribute(const WorldPosition&) -> HotCellAttribute*` (+ `const` overload) — `nullptr` when the containing chunk does not exist

- [ ] **Step 1: Write the failing test**

`tests/test_world.cpp`:

```cpp
#include <doctest/doctest.h>

#include <cellulose/world.hpp>

using cellulose::ChunkPosition;
using cellulose::WorldPosition;

TEST_CASE("a fresh world has no chunks") {
	cellulose::World<> world;
	CHECK(world.chunk_count() == 0);
	CHECK_FALSE(world.has_chunk(ChunkPosition{0, 0, 0}));
	CHECK(world.find_chunk(ChunkPosition{0, 0, 0}) == nullptr);
}

TEST_CASE("chunk() creates on first access and is idempotent afterwards") {
	cellulose::World<> world;

	auto &created = world.chunk(ChunkPosition{1, -2, 3});
	CHECK(world.chunk_count() == 1);
	CHECK(world.has_chunk(ChunkPosition{1, -2, 3}));

	created.hot_attribute(cellulose::LocalPosition{0, 0, 0}).block_id = 5;
	CHECK(&world.chunk(ChunkPosition{1, -2, 3}) == &created);
	CHECK(world.chunk_count() == 1);
	CHECK(world.find_chunk(ChunkPosition{1, -2, 3})->hot_attribute(cellulose::LocalPosition{0, 0, 0}).block_id == 5);
}

TEST_CASE("remove_chunk erases and reports whether anything was removed") {
	cellulose::World<> world;
	world.chunk(ChunkPosition{0, 0, 0});

	CHECK(world.remove_chunk(ChunkPosition{0, 0, 0}));
	CHECK(world.chunk_count() == 0);
	CHECK_FALSE(world.remove_chunk(ChunkPosition{0, 0, 0}));
}

TEST_CASE("for_each_chunk visits every chunk once") {
	cellulose::World<> world;
	world.chunk(ChunkPosition{0, 0, 0});
	world.chunk(ChunkPosition{1, 0, 0});
	world.chunk(ChunkPosition{0, 1, 0});

	cellulose::size visited = 0;
	world.for_each_chunk([&](const ChunkPosition &, cellulose::Chunk<> &) { ++visited; });
	CHECK(visited == 3);
}

TEST_CASE("find_hot_attribute resolves a world position through its chunk") {
	cellulose::World<> world;

	CHECK(world.find_hot_attribute(WorldPosition{-1, 40, 5}) == nullptr);

	world.chunk(cellulose::to_chunk_position(WorldPosition{-1, 40, 5}))
			.hot_attribute(cellulose::to_local_position(WorldPosition{-1, 40, 5}))
			.block_id = 77;

	auto *attribute = world.find_hot_attribute(WorldPosition{-1, 40, 5});
	REQUIRE(attribute != nullptr);
	CHECK(attribute->block_id == 77);

	// A neighbouring world position in the same chunk is still default.
	CHECK(world.find_hot_attribute(WorldPosition{-2, 40, 5})->block_id == 0);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake -S . -B build && cmake --build build --target cellulose_tests`
Expected: FAIL — `cellulose/world.hpp` not found.

- [ ] **Step 3: Write the implementation**

`inc/cellulose/world.hpp`:

```cpp
#ifndef CEL_WORLD_HPP
#define CEL_WORLD_HPP

#include "cell.hpp"
#include "chunk.hpp"
#include "coordinate.hpp"
#include "types.hpp"
#include <ankerl/unordered_dense.h>
#include <utility>

namespace cellulose {

namespace impl {

/// @brief The set of loaded chunks, indexed by chunk coordinate in a Robin-Hood hash table.
template <typename ChunkType = Chunk<>>
class World final {
public:
	using ChunkMap = ankerl::unordered_dense::map<ChunkPosition, ChunkType, ChunkPositionHash>;

private:
	ChunkMap m_chunks;

public:
	auto has_chunk(const ChunkPosition &p_position) const -> bool {
		return m_chunks.contains(p_position);
	}

	auto find_chunk(const ChunkPosition &p_position) -> ChunkType * {
		const auto iterator = m_chunks.find(p_position);
		return iterator == m_chunks.end() ? nullptr : &iterator->second;
	}

	auto find_chunk(const ChunkPosition &p_position) const -> const ChunkType * {
		const auto iterator = m_chunks.find(p_position);
		return iterator == m_chunks.end() ? nullptr : &iterator->second;
	}

	auto chunk(const ChunkPosition &p_position) -> ChunkType & {
		return m_chunks.try_emplace(p_position).first->second;
	}

	auto remove_chunk(const ChunkPosition &p_position) -> bool {
		return m_chunks.erase(p_position) != 0;
	}

	auto chunk_count() const -> size {
		return m_chunks.size();
	}

	template <typename Visitor>
	auto for_each_chunk(Visitor &&p_visitor) -> void {
		for (auto &[position, chunk] : m_chunks)
			std::forward<Visitor>(p_visitor)(position, chunk);
	}

	auto find_hot_attribute(const WorldPosition &p_world) -> HotCellAttribute * {
		auto *chunk = find_chunk(to_chunk_position(p_world));
		if (chunk == nullptr)
			return nullptr;
		return &chunk->hot_attribute(to_local_position(p_world));
	}

	auto find_hot_attribute(const WorldPosition &p_world) const -> const HotCellAttribute * {
		const auto *chunk = find_chunk(to_chunk_position(p_world));
		if (chunk == nullptr)
			return nullptr;
		return &chunk->hot_attribute(to_local_position(p_world));
	}
};

} //namespace impl

template <typename ChunkType = Chunk<>>
using World = impl::World<ChunkType>;

} //namespace cellulose

#endif // CEL_WORLD_HPP
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build --target cellulose_tests && ctest --test-dir build -R "world|chunk\(\)|remove_chunk|for_each_chunk|find_hot_attribute" --output-on-failure`
Expected: PASS — all five cases green.

- [ ] **Step 5: Run the full suite**

Run: `cmake -S . -B build && cmake --build build --target cellulose_tests && ctest --test-dir build --output-on-failure`
Expected: PASS — every test from Tasks 1–7 green.

- [ ] **Step 6: Commit**

```bash
git add inc/cellulose/world.hpp tests/test_world.cpp
git commit -m "Add `World` chunk container keyed by chunk coordinate."
```

---

## Task 8: Umbrella header, demo, and status note

**Files:**
- Create: `inc/cellulose/cellulose.hpp`
- Modify: `src/main.cpp` (add a stdout demo before `InitWindow`, around line 18)
- Modify: `README.md` (append a short status note)

**Interfaces:**
- Consumes: all public headers.
- Produces: `#include <cellulose/cellulose.hpp>` as the single entry point.

- [ ] **Step 1: Write the umbrella header**

`inc/cellulose/cellulose.hpp`:

```cpp
#ifndef CEL_CELLULOSE_HPP
#define CEL_CELLULOSE_HPP

#include "block.hpp"
#include "cell.hpp"
#include "chunk.hpp"
#include "coordinate.hpp"
#include "inspect.hpp"
#include "morton.hpp"
#include "types.hpp"
#include "world.hpp"

#endif // CEL_CELLULOSE_HPP
```

- [ ] **Step 2: Add a compile-check test**

Append to `tests/test_world.cpp` (top-level, after includes is fine — it is a separate TU concern, so instead add a new tiny file):

`tests/test_umbrella.cpp`:

```cpp
#include <doctest/doctest.h>

#include <cellulose/cellulose.hpp>

TEST_CASE("umbrella header pulls in World and Chunk") {
	cellulose::World<> world;
	world.chunk(cellulose::ChunkPosition{0, 0, 0}).fill_hot(cellulose::HotCellAttribute{1, 0});
	CHECK(world.find_hot_attribute(cellulose::WorldPosition{0, 0, 0})->block_id == 1);
}
```

- [ ] **Step 3: Update the demo**

In `src/main.cpp`, replace the existing `HotCellAttribute` demo block (lines ~15-18) with an include of `<cellulose/cellulose.hpp>` (replacing `<cellulose/cell.hpp>`) and:

```cpp
	cellulose::World<> world;
	auto &chunk = world.chunk(cellulose::ChunkPosition{0, 0, 0});
	chunk.hot_attribute(cellulose::LocalPosition{1, 2, 3}).block_id = 42;
	std::cout << "chunks loaded: " << world.chunk_count() << '\n';
	std::cout << "block at (1,2,3): "
			  << world.find_hot_attribute(cellulose::WorldPosition{1, 2, 3})->block_id << '\n';
```

- [ ] **Step 4: Build everything and run**

Run: `cmake -S . -B build && cmake --build build`
Then: `ctest --test-dir build --output-on-failure`
Expected: PASS — full suite green, `cellulose` executable links.

Run: `./build/cellulose` (Windows: `build/Debug/cellulose.exe` or `build/cellulose.exe` depending on generator) — expect the two `std::cout` lines (`chunks loaded: 1`, `block at (1,2,3): 42`) before the raylib window opens; close the window to exit.

- [ ] **Step 5: Add the status note to README**

Append a section to `README.md`:

```markdown
## Implementation Status

The foundation — `Chunk` (Morton-coded per-cell storage) and `World` (chunk hash table) —
is implemented and covered by the `cellulose_tests` suite (`ctest --test-dir build`).
Concrete parameters chosen ahead of a full spec (chunk edge length 32, `u32` Morton
index, `i64` world coordinates, `doctest` for tests) are recorded in
`docs/superpowers/plans/` alongside the plan that introduced them.
```

- [ ] **Step 6: Commit**

```bash
git add inc/cellulose/cellulose.hpp tests/test_umbrella.cpp src/main.cpp README.md
git commit -m "Add umbrella header, `World` demo, and implementation-status note."
```

---

## Verification (end-to-end)

1. **Clean configure + build:**
   `rm -rf build && cmake -S . -B build && cmake --build build`
   Expected: `libcellulose` (INTERFACE), `cellulose` (exe), and `cellulose_tests` all build with no warnings from `inc/cellulose/*`.
2. **Run the test suite:**
   `ctest --test-dir build --output-on-failure`
   Expected: all cases from Tasks 1–8 pass; `doctest_discover_tests` lists each `TEST_CASE` as its own CTest entry.
3. **Run the demo:**
   Launch the `cellulose` executable. Expected stdout before the window opens:
   ```
   chunks loaded: 1
   block at (1,2,3): 42
   ```
4. **Consumer check (optional):** from another CMake project, `add_subdirectory(cellulose)` and confirm `CELLULOSE_BUILD_TESTS` defaults OFF (no `cellulose_tests` target, no doctest fetch).
5. **Formatting:** `clang-format --dry-run --Werror inc/cellulose/*.hpp tests/*.cpp src/main.cpp` — clean.

## Self-Review notes

- **Spec coverage (README § Core Data Structure):** world-level hash table → Task 7; Morton-coded 1D array per chunk → Tasks 5–6; AoS hot data (<4 bytes, `HotCellAttribute` is 4) → Task 6 (`m_hot`); SoA packed / sparse-list freezing-cold tiers → Task 6 (`packed()` / `sparse()`, types deferred per A8); Robin-Hood table rationale (single world-level hash) → Task 7 uses one `unordered_dense::map`. Concurrency, spatial querying, and rendering are explicitly out of scope (separate plans).
- **Type consistency:** `CellIndex = u32` used identically in `morton.hpp` and `chunk.hpp`; `hot_attribute` overload set (`LocalPosition` + `CellIndex`, each const/non-const) consistent between `chunk.hpp` definition and `world.hpp` call sites; `find_hot_attribute` (not `hot_attribute`) is the World-level nullable accessor everywhere.
- **Known follow-ups (not this plan):** `PackedCellAttributeCollection` requires element `sizeof <= 8` while the README describes cold data as ">8 bytes" — reconcile when a concrete cold attribute type is introduced. `HotCellAttribute::get_pitch/get_yaw` return values in shifted bit-space (consistent with the pre-shifted enum constants) — left as-is.
