/*
 * queued_task.h
 * Copyright (C) 2021 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AVP_QUEUED_TASK_H
#define AVP_QUEUED_TASK_H

namespace avp {

class QueuedTask {
 public:
  virtual ~QueuedTask() = default;

  virtual bool run() = 0;
};

}  // namespace avp

#endif /* !AVP_QUEUED_TASK_H */
