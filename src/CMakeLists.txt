# Each milestone adds a directory here: frontend, sema, interp, ir, passes, hls,
# rtl, driver. Milestone 0 only needs the shared fixed-width integer support.
add_library(minihls_support
  support/bits.cpp
  support/bits.hpp)

target_include_directories(minihls_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(minihls_support PRIVATE minihls_warnings)
