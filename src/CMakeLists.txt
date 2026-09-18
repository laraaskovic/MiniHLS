# Each milestone adds a directory here: sema, interp, ir, passes, hls, rtl,
# driver. Milestone 0 added the shared integer support; milestone 1 the frontend.
add_library(minihls_support
  support/bits.cpp
  support/bits.hpp)

target_include_directories(minihls_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(minihls_support PRIVATE minihls_warnings)

add_library(minihls_frontend
  frontend/source.cpp
  frontend/source.hpp
  frontend/diagnostics.cpp
  frontend/diagnostics.hpp
  frontend/token.cpp
  frontend/token.hpp
  frontend/lexer.cpp
  frontend/lexer.hpp
  frontend/ast.cpp
  frontend/ast.hpp
  frontend/ast_printer.cpp
  frontend/ast_printer.hpp
  frontend/parser.cpp
  frontend/parser.hpp)

target_include_directories(minihls_frontend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(minihls_frontend
  PUBLIC minihls_support
  PRIVATE minihls_warnings)

add_subdirectory(driver)
