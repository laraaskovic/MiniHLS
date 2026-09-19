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
  interp/ast_interp.hpp
  interp/random_inputs.cpp
  interp/random_inputs.hpp)

target_link_libraries(minihls_interp
  PUBLIC minihls_sema
  PRIVATE minihls_warnings)

if(MINIHLS_MLIR)
  # MLIRGen: the checked AST to func/arith/scf/memref (milestone P3).
  add_library(minihls_mlirgen
    mlirgen/mlirgen.cpp
    mlirgen/mlirgen.hpp)
  target_link_libraries(minihls_mlirgen
    PUBLIC minihls_sema minihls_mlir_settings
           MLIRIR MLIRArithDialect MLIRFuncDialect MLIRMemRefDialect MLIRSCFDialect
    PRIVATE minihls_warnings)

  # The optimization pipeline: MLIR's own passes plus ours (milestone P4).
  add_library(minihls_transforms
    transforms/transforms.cpp
    transforms/transforms.hpp)
  target_link_libraries(minihls_transforms
    PUBLIC minihls_mlir_settings
           MLIRIR MLIRPass MLIRTransforms
           MLIRArithDialect MLIRSCFDialect MLIRSCFUtils
    PRIVATE minihls_warnings)

  # The execution check: lower to LLVM, JIT, and run against test vectors.
  add_library(minihls_mlir_execution
    mlirgen/execution.cpp
    mlirgen/execution.hpp)
  target_link_libraries(minihls_mlir_execution
    PUBLIC minihls_mlirgen minihls_interp
    PRIVATE minihls_warnings
            MLIRPass MLIRExecutionEngine
            MLIRSCFToControlFlow MLIRArithToLLVM MLIRMemRefToLLVM MLIRFuncToLLVM
            MLIRControlFlowToLLVM MLIRReconcileUnrealizedCasts
            MLIRBuiltinToLLVMIRTranslation MLIRLLVMToLLVMIRTranslation)
endif()

add_subdirectory(driver)
