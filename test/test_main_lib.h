/*
 * test_main_lib.h
 * Copyright (C) 2023 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */
#ifndef TEST_TEST_MAIN_LIB_H_
#define TEST_TEST_MAIN_LIB_H_

#include <memory>
#include <string>

namespace ave {

// Class to initialize test environment and run tests.
class TestMain {
 public:
  virtual ~TestMain() {}

  static std::unique_ptr<TestMain> Create();

  // Initializes test environment. Clients can add their own initialization
  // steps after call to this method and before running tests.
  // Returns 0 if initialization was successful and non 0 otherwise.
  virtual int Init() = 0;
  // Temporary for backward compatibility
  virtual int Init(int* argc, char* argv[]) = 0;

  // Runs test end return result error code. 0 - no errors.
  virtual int Run(int argc, char* argv[]) = 0;

 protected:
  TestMain() = default;
};

}  // namespace ave

#endif  // TEST_TEST_MAIN_LIB_H_
