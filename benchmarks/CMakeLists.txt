# Built only when CELLULOSE_BUILD_BENCHMARKS=ON (see the root CMakeLists).
# Measurement is meaningless without optimisation — build Release.

file(GLOB BENCH_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable(cellulose_benchmarks ${BENCH_SOURCES})
target_link_libraries(cellulose_benchmarks PRIVATE libcellulose)
target_compile_features(cellulose_benchmarks PRIVATE cxx_std_20)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(WARNING "cellulose_benchmarks: no build type set — configure with -DCMAKE_BUILD_TYPE=Release.")
endif()
