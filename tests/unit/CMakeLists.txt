add_executable(minihls_unit_tests
  support/bits_test.cpp)

target_link_libraries(minihls_unit_tests
  PRIVATE minihls_support minihls_warnings GTest::gtest_main)

add_test(NAME unit COMMAND minihls_unit_tests)
