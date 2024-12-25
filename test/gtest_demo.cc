/*
 * test_demo.cpp
 * Copyright (C) 2021 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "gtest.h"

TEST(DemoTest, test1) {
  ASSERT_TRUE(1 > 0);
}

int main(int argc, char* argv[]) {
  ::testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
