# Tests of the MLIR-based stages. Built only when MLIR and CIRCT are found.
add_executable(minihls_mlir_tests
  smoke_test.cpp
  mlirgen_test.cpp
  transforms_test.cpp
  generated.hpp)

target_link_libraries(minihls_mlir_tests
  PRIVATE
    minihls_mlir_execution
    minihls_mlirgen
    minihls_transforms
    minihls_interp
    minihls_mlir_settings
    minihls_warnings
    GTest::gtest_main
    MLIRIR
    MLIRFuncDialect
    MLIRArithDialect
    CIRCTHW
    CIRCTComb)

target_compile_definitions(minihls_mlir_tests
  PRIVATE MINIHLS_EXAMPLES_DIR="${PROJECT_SOURCE_DIR}/examples")

add_test(NAME mlir COMMAND minihls_mlir_tests)
