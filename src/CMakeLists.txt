# One library per stage, each depending only on the stages before it:
# support -> frontend -> sema -> interp, and later mlirgen -> transforms -> hls.
add_library(minihls_support
  support/bits.cpp
  support/bits.hpp
  support/int128.hpp)

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

add_library(minihls_sema
  sema/semantics.cpp
  sema/semantics.hpp
  sema/sema.cpp
  sema/sema.hpp)

target_link_libraries(minihls_sema
  PUBLIC minihls_frontend
  PRIVATE minihls_warnings)

add_library(minihls_interp
  interp/io.cpp
  interp/io.hpp
  interp/ast_interp.cpp
  interp/ast_interp.hpp)

target_link_libraries(minihls_interp
  PUBLIC minihls_sema
  PRIVATE minihls_warnings)

add_subdirectory(driver)
