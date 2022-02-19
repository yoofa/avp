/*
 * logging_test.cc
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "base/logging.h"
#include <iostream>

using namespace avp;

int main(int argc, char const* argv[]) {
  LOG(LS_INFO) << "log info1";
  LOG(LS_DEBUG) << "log debug1";
  avp::LogMessage::LogToDebug(avp::LogSeverity::LS_VERBOSE);
  LOG(LS_INFO) << "log info2";
  LOG(LS_INFO) << "log debug2";
  return 0;
}
