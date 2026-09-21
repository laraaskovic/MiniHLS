include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
)
FetchContent_MakeAvailable(googletest)

add_executable(minihls_tests version_test.cpp bits_test.cpp lexer_test.cpp parser_test.cpp resolver_test.cpp type_check_test.cpp const_eval_test.cpp interpreter_test.cpp)
target_link_libraries(minihls_tests PRIVATE minihls_core GTest::gtest_main)
target_compile_definitions(minihls_tests PRIVATE
  MINIHLS_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  MINIHLS_EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/examples")

# The emitter's tests only exist in an MLIR build, so they go in their own
# binary rather than under an #ifdef inside minihls_tests.
if(MINIHLS_ENABLE_MLIR)
  add_executable(minihls_mlir_tests mlirgen_test.cpp)
  target_link_libraries(minihls_mlir_tests PRIVATE minihls_mlir GTest::gtest_main)
endif()

include(GoogleTest)
gtest_discover_tests(minihls_tests)
if(MINIHLS_ENABLE_MLIR)
  gtest_discover_tests(minihls_mlir_tests)
endif()
# The examples, end to end: front end, interpreter, and the hand-computed
# vectors in each examples/<name>.tests.
foreach(example max3 abs_diff popcount dot fir stream_sum)
  add_test(NAME example.${example}
           COMMAND minihls test ${CMAKE_SOURCE_DIR}/examples/${example}.hc)
endforeach()

# E4.S2 is done when the emitted IR passes the verifier, which `emit-mlir`
# runs before printing. Only max3 for now; each later story adds its example.
if(MINIHLS_ENABLE_MLIR)
  foreach(example max3)
    add_test(NAME mlir.${example}
             COMMAND minihls emit-mlir ${CMAKE_SOURCE_DIR}/examples/${example}.hc)
  endforeach()
endif()
