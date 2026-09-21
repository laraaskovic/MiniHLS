# src/CMakeLists.txt
add_library(minihls_core
  version.cpp
  support/bits.cpp
  frontend/source.cpp
  frontend/token.cpp
  frontend/diagnostics.cpp
  frontend/lexer.cpp
  frontend/parser.cpp
  frontend/ast_printer.cpp
  sema/resolver.cpp
  sema/type_check.cpp
  sema/const_eval.cpp
  interp/interpreter.cpp
  driver/compile.cpp
  driver/testfile.cpp
  driver/run.cpp)
target_include_directories(minihls_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(minihls_core PRIVATE minihls_warnings)

add_executable(minihls main.cpp)
target_link_libraries(minihls PRIVATE minihls_core minihls_warnings)

if(MINIHLS_ENABLE_MLIR)
  add_library(minihls_mlir dump.cpp mlirgen/mlirgen.cpp)
  target_include_directories(minihls_mlir PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  # SYSTEM: the project builds with -Wall -Wextra -Wpedantic, and the MLIR and
  # CIRCT headers do not. Without this every translation unit that includes
  # them buries its own warnings under thousands of theirs.
  target_include_directories(minihls_mlir SYSTEM PUBLIC
    ${LLVM_INCLUDE_DIRS}
    ${MLIR_INCLUDE_DIRS}
    ${CIRCT_INCLUDE_DIRS})
  target_link_libraries(minihls_mlir PUBLIC
    minihls_core                      # the emitter walks the front end's AST
    MLIRIR MLIRParser
    MLIRFuncDialect MLIRArithDialect MLIRSCFDialect MLIRMemRefDialect
    CIRCTHW CIRCTComb CIRCTSeq)
  target_compile_options(minihls_mlir PRIVATE -fno-rtti)
  target_link_libraries(minihls_mlir PRIVATE minihls_warnings)

  target_link_libraries(minihls PRIVATE minihls_mlir)
  target_compile_definitions(minihls PRIVATE MINIHLS_WITH_MLIR)
endif()