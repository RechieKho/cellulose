include(FetchContent)

FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.5.3
)
FetchContent_MakeAvailable(doctest)

# `main.cpp` defines the doctest entry point; every `test_*.cpp` is a case file.
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp")

add_executable(cellulose_tests ${TEST_SOURCES})
target_link_libraries(cellulose_tests PRIVATE libcellulose doctest::doctest)

set(CELLULOSE_SANITIZER "" CACHE STRING
    "Sanitizer to build the tests with: '' (none), thread, address, undefined. \
ThreadSanitizer is Linux/macOS only.")
if(CELLULOSE_SANITIZER)
    if(MSVC)
        # MSVC only supports the address sanitizer.
        target_compile_options(cellulose_tests PRIVATE /fsanitize=${CELLULOSE_SANITIZER})
    else()
        target_compile_options(cellulose_tests PRIVATE
            -fsanitize=${CELLULOSE_SANITIZER} -fno-omit-frame-pointer -g)
        target_link_options(cellulose_tests PRIVATE -fsanitize=${CELLULOSE_SANITIZER})
    endif()
endif()

list(APPEND CMAKE_MODULE_PATH "${doctest_SOURCE_DIR}/scripts/cmake")
include(doctest)
doctest_discover_tests(cellulose_tests)
