cmake_minimum_required(VERSION 3.20)
project(minihls LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)          # -std=c++20, not -std=gnu++20

add_compile_options(-Wall -Wextra -Wpedantic)

enable_testing()                        # must be here, at the top level

add_subdirectory(src)
add_subdirectory(tests)