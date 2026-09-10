# Built only when CELLULOSE_BUILD_BENCHMARKS=ON (see the root CMakeLists).
# Measurement is meaningless without optimisation — build Release.

file(GLOB BENCH_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable(cellulose_benchmarks ${BENCH_SOURCES})
target_link_libraries(cellulose_benchmarks PRIVATE libcellulose)
target_compile_features(cellulose_benchmarks PRIVATE cxx_std_20)

# Same CELLULOSE_SANITIZER knob as the tests (defined there or on the command
# line): the workload benchmark is the thing we want to run under TSan.
if(CELLULOSE_SANITIZER AND NOT MSVC)
    target_compile_options(cellulose_benchmarks PRIVATE
        -fsanitize=${CELLULOSE_SANITIZER} -fno-omit-frame-pointer -g)
    target_link_options(cellulose_benchmarks PRIVATE -fsanitize=${CELLULOSE_SANITIZER})
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(WARNING "cellulose_benchmarks: no build type set — configure with -DCMAKE_BUILD_TYPE=Release.")
endif()
