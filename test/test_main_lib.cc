/*
 * test_main_lib.cc
 * Copyright (C) 2023 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */
#include "test/test_main_lib.h"

#include <fstream>
#include <memory>
#include <string>

#include "base/logging.h"
#include "test/gtest.h"

namespace ave {

namespace {

class TestMainImpl : public TestMain {
 public:
  // In order to set up a fresh rtc::Thread state for each test and avoid
  // accidentally carrying over pending tasks that might be sent from one test
  // and executed while another test is running, we inject a TestListener
  // that sets up a new rtc::Thread instance for the main thread, per test.
  class TestListener : public ::testing::EmptyTestEventListener {
   public:
    TestListener() = default;

   private:
    bool IsDeathTest(const char* test_case_name, const char* test_name) {
      return false;
    }

    void OnTestStart(const ::testing::TestInfo& test_info) override {}

    void OnTestEnd(const ::testing::TestInfo& test_info) override {}
  };

  int Init(int* argc, char* argv[]) override { return Init(); }

  int Init() override {
    ::testing::UnitTest::GetInstance()->listeners().Append(new TestListener());

    return 0;
  }

  int Run(int argc, char* argv[]) override {
    int exit_code = RUN_ALL_TESTS();

    return exit_code;
  }

  ~TestMainImpl() override = default;

 private:
};

}  // namespace

std::unique_ptr<TestMain> TestMain::Create() {
  return std::make_unique<TestMainImpl>();
}

}  // namespace ave
