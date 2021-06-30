/*
 * task_queue_base.h
 * Copyright (C) 2021 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AVP_TASK_QUEUE_BASE_H
#define AVP_TASK_QUEUE_BASE_H

#include <memory>

#include "queued_task.h"

namespace avp {

class TaskQueueBase {
 public:
  virtual ~TaskQueueBase() = default;

  virtual void Delete() = 0;

  virtual void PostTask(std::unique_ptr<QueuedTask> task) = 0;

 private:
  /* data */
};

struct TaskQueueDeleter {
  void operator()(TaskQueueBase* task_queue) const { task_queue->Delete(); }
};

}  // namespace avp

#endif /* !AVP_TASK_QUEUE_BASE_H */
