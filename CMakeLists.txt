cmake_minimum_required(VERSION 3.10)

# Project name and version
project(cppq VERSION 1.0 LANGUAGES CXX)

# Specify C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Add header-only library
add_library(cppq INTERFACE)

# Specify the include directory for the library
target_include_directories(cppq INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Executable for tests
add_executable(tests tests.cpp)
target_link_libraries(tests PRIVATE
    cppq
    hiredis
    uuid
    Catch2Main
    Catch2)

# Executable for example
add_executable(example example.cpp)
target_link_libraries(example PRIVATE
    cppq
    hiredis
    uuid)