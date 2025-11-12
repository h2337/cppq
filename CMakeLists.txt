# ------------------------------------------------------------
# Minimum CMake version required
# ------------------------------------------------------------
cmake_minimum_required(VERSION 3.10)

# ------------------------------------------------------------
# Project definition
# ------------------------------------------------------------
# - Name: cppq
# - Version: 0.1
# - Language: C++
project(cppq VERSION 0.1 LANGUAGES CXX)

# ------------------------------------------------------------
# C++ standard configuration
# ------------------------------------------------------------
# Require modern C++17 support (no compiler extensions)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ------------------------------------------------------------
# Header-only library definition
# ------------------------------------------------------------
# This creates an INTERFACE library — no compiled code,
# just headers and interface properties (include dirs, etc.)
add_library(cppq INTERFACE)

# Make headers in the project root available to anything
# that links to cppq
target_include_directories(cppq INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# ------------------------------------------------------------
# Dependency discovery
# ------------------------------------------------------------
# We’ll use pkg-config to find libraries that don’t provide
# native CMake config files.

# Make sure pkg-config is available
find_package(PkgConfig REQUIRED)

# Find libuuid (used for generating UUIDs)
pkg_check_modules(UUID REQUIRED uuid)

# Find hiredis (Redis C client library)
pkg_check_modules(HIREDIS REQUIRED hiredis)

# Find Catch2 v3 (C++ testing framework)
find_package(Catch2 3 REQUIRED)

# ------------------------------------------------------------
# Build the unit test executable
# ------------------------------------------------------------
# - Compiles tests.cpp
# - Links against cppq, hiredis, uuid, and Catch2
add_executable(tests tests.cpp)

# Add include directories from pkg-config results
target_include_directories(tests PRIVATE
        ${UUID_INCLUDE_DIRS}
        ${HIREDIS_INCLUDE_DIRS}
)

# Link required libraries
target_link_libraries(tests PRIVATE
        cppq                 # our header-only library
        ${UUID_LIBRARIES}    # libuuid
        ${HIREDIS_LIBRARIES} # hiredis
        Catch2::Catch2WithMain # Catch2 test runner
)

# ------------------------------------------------------------
# Build the example executable
# ------------------------------------------------------------
# - Compiles example.cpp
# - Links only with cppq, hiredis, and uuid
add_executable(example example.cpp)

# Include hiredis and uuid headers
target_include_directories(example PRIVATE
        ${UUID_INCLUDE_DIRS}
        ${HIREDIS_INCLUDE_DIRS}
)

# Link with cppq and dependencies
target_link_libraries(example PRIVATE
        cppq
        ${UUID_LIBRARIES}
        ${HIREDIS_LIBRARIES}
)
