// tests/version_test.cpp
#include <gtest/gtest.h>
#include "version.hpp"

TEST(Version, IsNotEmpty) {
  EXPECT_FALSE(minihls::version().empty());
}