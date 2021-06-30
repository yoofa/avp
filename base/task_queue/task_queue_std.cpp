/*
 * task_queue_std.cpp
 * Copyright (C) 2021 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "task_queue_std.h"

class StdTaskQueueFactory final : public TaskQueueFactory {
 public:
  StdTaskQueueFactory(arguments);
  virtual ~StdTaskQueueFactory();

 private:
  /* data */
};
