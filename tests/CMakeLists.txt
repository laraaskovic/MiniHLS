include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
)
FetchContent_MakeAvailable(googletest)

add_executable(minihls_tests version_test.cpp bits_test.cpp)
target_link_libraries(minihls_tests PRIVATE minihls_core GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(minihls_tests)