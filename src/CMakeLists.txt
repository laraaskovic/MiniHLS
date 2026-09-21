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
  sema/resolver.cpp)
target_include_directories(minihls_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(minihls main.cpp)
target_link_libraries(minihls PRIVATE minihls_core)

if(MINIHLS_ENABLE_MLIR)
  add_library(minihls_mlir dump.cpp)
  target_include_directories(minihls_mlir PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${LLVM_INCLUDE_DIRS}
    ${MLIR_INCLUDE_DIRS}
    ${CIRCT_INCLUDE_DIRS})
  target_link_libraries(minihls_mlir PUBLIC
    MLIRIR MLIRParser
    MLIRFuncDialect MLIRArithDialect MLIRSCFDialect MLIRMemRefDialect
    CIRCTHW CIRCTComb CIRCTSeq)
  target_compile_options(minihls_mlir PRIVATE -fno-rtti)

  target_link_libraries(minihls PRIVATE minihls_mlir)
  target_compile_definitions(minihls PRIVATE MINIHLS_WITH_MLIR)
endif()