/*
 * task_queue.h
 * Copyright (C) 2021 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AVP_TASK_QUEUE_H
#define AVP_TASK_QUEUE_H

#include <memory>

#include "queued_task.h"

namespace avp {

class TaskQueue {
 public:
  virtual ~TaskQueue();

  void postTask(std::unique_ptr<avp::QueuedTask> task);

  template <typename Closure>
  void postTask(Closure&& closure) {}

 private:
  /* data */
};
}  // namespace avp

#endif /* !AVP_TASK_QUEUE_H */
