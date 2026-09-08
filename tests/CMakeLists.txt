include(FetchContent)

FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.11
)

block(SCOPE_FOR VARIABLES)
    # doctest v2.4.11 declares `cmake_minimum_required(VERSION 3.0)`, which
    # CMake >= 4 rejects. Relax the policy floor just for this dependency.
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    FetchContent_MakeAvailable(doctest)
endblock()

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable(cellulose_tests ${TEST_SOURCES})
target_link_libraries(cellulose_tests PRIVATE libcellulose doctest::doctest)

list(APPEND CMAKE_MODULE_PATH "${doctest_SOURCE_DIR}/scripts/cmake")
include(doctest)
doctest_discover_tests(cellulose_tests)
