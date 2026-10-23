include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
)
FetchContent_MakeAvailable(googletest)

add_executable(minihls_tests version_test.cpp bits_test.cpp lexer_test.cpp parser_test.cpp resolver_test.cpp type_check_test.cpp const_eval_test.cpp interpreter_test.cpp)
target_link_libraries(minihls_tests PRIVATE minihls_core minihls_warnings GTest::gtest_main)
target_compile_definitions(minihls_tests PRIVATE
  MINIHLS_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  MINIHLS_EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/examples")

# The emitter's tests only exist in an MLIR build, so they go in their own
# binary rather than under an #ifdef inside minihls_tests.
if(MINIHLS_ENABLE_MLIR)
  add_executable(minihls_mlir_tests mlirgen_test.cpp)
  target_link_libraries(minihls_mlir_tests PRIVATE minihls_mlir minihls_warnings GTest::gtest_main)
endif()

include(GoogleTest)
gtest_discover_tests(minihls_tests)
if(MINIHLS_ENABLE_MLIR)
  gtest_discover_tests(minihls_mlir_tests)
endif()
# The examples, end to end: front end, interpreter, and the hand-computed
# vectors in each examples/<name>.tests.
foreach(example max3 abs_diff dot fir stream_sum)
  add_test(NAME example.${example}
           COMMAND minihls test ${CMAKE_SOURCE_DIR}/examples/${example}.hc)
endforeach()

# The emitted IR has to pass the verifier, which `emit-mlir` runs before it
# prints. Each story adds the example it unlocks. No example exercises a
# loop without also using arrays, so S6's end-to-end coverage lives in
# tests/mlirgen/for_*.hc and dot/fir/stream_sum arrive with S7 and E6.
if(MINIHLS_ENABLE_MLIR)
  foreach(example max3 abs_diff)
    add_test(NAME mlir.${example}
             COMMAND minihls emit-mlir ${CMAKE_SOURCE_DIR}/examples/${example}.hc)
  endforeach()
endif()

# One FileCheck test per row of LANGUAGE.md's width table, plus the two scf.if
# shapes. The expectations live inside each .hc input as `// CHECK:` comments,
# lit-style, so a program and the IR it must produce are never separated.
if(MINIHLS_ENABLE_MLIR)
  find_program(FILECHECK_EXE NAMES FileCheck
               HINTS ${LLVM_TOOLS_BINARY_DIR} ${LLVM_BINARY_DIR}/bin)
  if(FILECHECK_EXE)
    file(GLOB MLIRGEN_CASES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/mlirgen
         ${CMAKE_CURRENT_SOURCE_DIR}/mlirgen/*.hc)
    foreach(case ${MLIRGEN_CASES})
      string(REPLACE ".hc" "" name ${case})
      add_test(NAME mlirgen.${name} COMMAND ${CMAKE_COMMAND}
               -DMINIHLS=$<TARGET_FILE:minihls>
               -DFILECHECK=${FILECHECK_EXE}
               -DCASE=${CMAKE_CURRENT_SOURCE_DIR}/mlirgen/${case}
               -P ${CMAKE_CURRENT_SOURCE_DIR}/RunFileCheck.cmake)
    endforeach()
  else()
    message(STATUS "minihls: FileCheck not found, skipping the mlirgen.* tests")
  endif()
endif()

# E8 rung one, end to end: the combinational examples must lower to hw/comb and
# then through ExportVerilog into SystemVerilog that Verilator accepts. Only
# max3 and abs_diff are combinational; everything with a loop or an array needs
# a cycle count, so it needs E7 and E8's FSM.
if(MINIHLS_ENABLE_MLIR)
  find_program(VERILATOR_EXE NAMES verilator)
  foreach(example max3 abs_diff)
    add_test(NAME hw.${example}
             COMMAND minihls emit-hw ${CMAKE_SOURCE_DIR}/examples/${example}.hc)
    if(VERILATOR_EXE)
      add_test(NAME verilog.${example} COMMAND ${CMAKE_COMMAND}
               -DMINIHLS=$<TARGET_FILE:minihls>
               -DVERILATOR=${VERILATOR_EXE}
               -DCASE=${CMAKE_SOURCE_DIR}/examples/${example}.hc
               -DNAME=${example}
               -P ${CMAKE_CURRENT_SOURCE_DIR}/RunVerilator.cmake)
    endif()
  endforeach()
  if(NOT VERILATOR_EXE)
    message(STATUS "minihls: verilator not found, skipping the verilog.* tests")
  endif()
endif()

# Co-simulation: the same vectors through the reference interpreter and through
# the generated hardware, which have to agree bit for bit. `minihls test`
# already checks the interpreter against examples/<name>.tests, so comparing
# the hardware against that file compares it against the interpreter.
#
# This is the check that distinguishes "the Verilog elaborates" from "the
# Verilog is correct", and it covers the defined-behaviour cases that only
# exist because LANGUAGE.md refused to leave them undefined.
if(MINIHLS_ENABLE_MLIR AND VERILATOR_EXE)
  foreach(example max3 abs_diff)
    add_test(NAME cosim.${example} COMMAND ${CMAKE_COMMAND}
             -DMINIHLS=$<TARGET_FILE:minihls>
             -DVERILATOR=${VERILATOR_EXE}
             -DCASE=${CMAKE_SOURCE_DIR}/examples/${example}.hc
             -DTESTS=${CMAKE_SOURCE_DIR}/examples/${example}.tests
             -DCOSIM_DIR=${CMAKE_CURRENT_SOURCE_DIR}/cosim
             -DNAME=${example}
             -P ${CMAKE_CURRENT_SOURCE_DIR}/RunCosim.cmake)
  endforeach()
endif()
