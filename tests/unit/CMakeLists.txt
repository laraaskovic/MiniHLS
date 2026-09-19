add_executable(minihls_unit_tests
  support/bits_test.cpp
  frontend/lexer_test.cpp
  frontend/parser_test.cpp
  frontend/roundtrip_test.cpp
  sema/sema_test.cpp
  interp/ast_interp_test.cpp)

target_link_libraries(minihls_unit_tests
  PRIVATE minihls_support minihls_frontend minihls_sema minihls_interp minihls_warnings
          GTest::gtest_main)

# The round-trip test walks examples/ rather than embedding a copy of each
# program, so it picks up new examples automatically.
target_compile_definitions(minihls_unit_tests
  PRIVATE MINIHLS_EXAMPLES_DIR="${PROJECT_SOURCE_DIR}/examples")

add_test(NAME unit COMMAND minihls_unit_tests)
