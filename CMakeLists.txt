# 3.24 for FetchContent's FIND_PACKAGE_ARGS, 3.25 for the SYSTEM target property
# used to keep Verilator's generated headers out of our diagnostics.
cmake_minimum_required(VERSION 3.25)

project(minihls
  VERSION 0.1.0
  DESCRIPTION "High-level synthesis compiler from a small C-like language to SystemVerilog"
  LANGUAGES CXX)

# Let find_package() honour VERILATOR_ROOT, which is how Verilator points at an
# install outside the default prefixes.
if(POLICY CMP0144)
  cmake_policy(SET CMP0144 NEW)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(MINIHLS_COSIM "Build the Verilator co-simulation test suite" ON)
option(MINIHLS_WERROR "Treat compiler warnings as errors" OFF)
option(MINIHLS_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

# Sanitizers have to reach the whole program, including dependencies, so they go
# on as directory-wide options rather than through the warnings interface target.
# Milestone 8 makes these part of routine verification; they are available now
# because a tree-walking compiler is exactly the kind of code that hides
# use-after-move and out-of-bounds bugs.
if(MINIHLS_SANITIZE)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
  else()
    message(WARNING "MINIHLS_SANITIZE is only supported for GCC and Clang")
  endif()
endif()

# Warning flags live on an interface target so that only our own code is held to
# them; vendored dependencies and Verilator-generated sources are not.
add_library(minihls_warnings INTERFACE)
target_compile_options(minihls_warnings INTERFACE
  $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Wshadow;-Wconversion>
  $<$<AND:$<BOOL:${MINIHLS_WERROR}>,$<CXX_COMPILER_ID:GNU,Clang,AppleClang>>:-Werror>
  $<$<CXX_COMPILER_ID:MSVC>:/W4>
  $<$<AND:$<BOOL:${MINIHLS_WERROR}>,$<CXX_COMPILER_ID:MSVC>>:/WX>)

include(FetchContent)

# Prefer an installed GoogleTest and fall back to fetching a pinned release, so
# a machine without network access can still build once the package is present.
FetchContent_Declare(googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.15.2
  GIT_SHALLOW TRUE
  EXCLUDE_FROM_ALL
  FIND_PACKAGE_ARGS NAMES GTest)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_subdirectory(src)
add_subdirectory(tests)

message(STATUS "minihls: build type      ${CMAKE_BUILD_TYPE}")
message(STATUS "minihls: co-simulation   ${MINIHLS_COSIM}")
