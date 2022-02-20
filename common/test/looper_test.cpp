/*
 * looper_test.cpp
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "test/gtest.h"

TEST(LooperTest, testInt) {
  ASSERT_TRUE(1 > 0);
}

int main(int argc, char* argv[]) {
  ::testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
