cmake_minimum_required(VERSION 3.20)
project(minihls LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)          # -std=c++20, not -std=gnu++20

# Warnings belong to OUR targets, not to every target in the build.
# add_compile_options() is directory-scoped, so it also lands on googletest,
# which FetchContent builds from source and which nobody here can fix. Link
# this interface library instead and the flags follow the code we own.
add_library(minihls_warnings INTERFACE)
target_compile_options(minihls_warnings INTERFACE -Wall -Wextra -Wpedantic)

option(MINIHLS_ENABLE_MLIR "Build the MLIR-dependent stages" OFF)

if(MINIHLS_ENABLE_MLIR)
  find_package(MLIR REQUIRED CONFIG)
  find_package(CIRCT REQUIRED CONFIG)
  message(STATUS "minihls: MLIR stages ON (MLIR_DIR=${MLIR_DIR})")
else()
  message(STATUS "minihls: MLIR stages OFF")
endif()


enable_testing()                        # must be here, at the top level

add_subdirectory(src)
add_subdirectory(tests)