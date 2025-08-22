/*
 * test_main.cc
 * Copyright (C) 2023 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */
#include <memory>

#include "test/gmock.h"
#include "test/test_main_lib.h"

int main(int argc, char* argv[]) {
  testing::InitGoogleMock(&argc, argv);

  std::unique_ptr<ave::TestMain> main = ave::TestMain::Create();
  int err_code = main->Init();
  if (err_code != 0) {
    return err_code;
  }
  return main->Run(argc, argv);
}
