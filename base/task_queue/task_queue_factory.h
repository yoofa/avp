/*
 * task_queue_factory.h
 * Copyright (C) 2021 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AVP_TASK_QUEUE_FACTORY_H
#define AVP_TASK_QUEUE_FACTORY_H

#include <memory>

#include "task_queue_base.h"

namespace avp {

class TaskQueueFactory {
 public:
  enum class Priority { NORMAL = 0, HIGH, LOW };

  virtual ~TaskQueueFactory() = default;

  virtual std::unique_ptr<TaskQueueBase, TaskQueueDeleter> createTaskQueue(
      std::string name, Priority priority) const = 0;
};

}  // namespace avp

#endif /* !AVP_TASK_QUEUE_FACTORY_H */
