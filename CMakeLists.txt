cmake_minimum_required(VERSION 3.10)

find_package(Git QUIET)
if(GIT_FOUND)
    # Run git describe to get the latest tag
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --dirty
        OUTPUT_VARIABLE GIT_TAG_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()
if (NOT GIT_TAG_VERSION)
    set(GIT_TAG_VERSION "0.0.1")
endif()
message(STATUS "Detected Tag Version: ${GIT_TAG_VERSION}")

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/PROJECT_NAME" PROJECT_NAME)
message(STATUS "Detected Project Name: ${PROJECT_NAME}")

project(
    "${PROJECT_NAME}"
    VERSION "${GIT_TAG_VERSION}"
    LANGUAGES CXX C
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(raylib QUIET)
find_package(Threads REQUIRED)

if(NOT TARGET raylib AND NOT raylib_FOUND)
include(FetchContent)

FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    VERSION 6.0
)

block(SCOPE_FOR VARIABLES)
    # Configuration variables for `raylib`.
    set(BUILD_EXAMPLES OFF CACHE INTERNAL "")
    FetchContent_MakeAvailable(raylib)
endblock()
endif()

set(THIRDPARTY_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty")

block(SCOPE_FOR VARIABLES)
    set(UNORDERED_DENSE_DIRECTORY "${THIRDPARTY_DIRECTORY}/unordered_dense")
    add_subdirectory("${UNORDERED_DENSE_DIRECTORY}")
endblock()

block(SCOPE_FOR VARIABLES)
    set(FMT_DIRECTORY "${THIRDPARTY_DIRECTORY}/fmt")
    add_subdirectory("${FMT_DIRECTORY}")
endblock()

set(EXECUTABLE_NAME "${PROJECT_NAME}")
set(LIBRARY_NAME "lib${PROJECT_NAME}")
string(MAKE_C_IDENTIFIER "${LIBRARY_NAME}" C_IDENTIFIER_LIBRARY_NAME)
set(SOURCE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(INCLUDE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/inc")
file(GLOB_RECURSE SOURCE_FILES "${SOURCE_DIRECTORY}/*.cpp")

# The core library is header-only and renderer-free: raylib is NOT an interface
# dependency, so `cellulose_tests` and any embedder linking `libcellulose` do not
# pull the renderer. Only the demo executable links raylib.
add_library(${LIBRARY_NAME} INTERFACE)
target_include_directories(${LIBRARY_NAME} INTERFACE ${INCLUDE_DIRECTORY})
target_link_libraries(${LIBRARY_NAME}
    INTERFACE
    unordered_dense
    fmt
    Threads::Threads
)

add_executable(${EXECUTABLE_NAME} ${SOURCE_FILES})
target_link_libraries(
    ${EXECUTABLE_NAME}
    PRIVATE
    ${LIBRARY_NAME}
    raylib
)

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

get_directory_property(HAS_PARENT PARENT_DIRECTORY)

if(HAS_PARENT)
    # Safe to use PARENT_SCOPE
    set("${PROJECT_NAME}_LIBRARY_NAME" "${LIBRARY_NAME}" PARENT_SCOPE)
endif()
