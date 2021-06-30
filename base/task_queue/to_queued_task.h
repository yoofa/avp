/*
 * to_queued_task.h
 * Copyright (C) 2021 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AVP_TO_QUEUED_TASK_H
#define AVP_TO_QUEUED_TASK_H

#include <memory>
#include <utility>

#include "queued_task.h"

namespace avp {

namespace internal {

template <typename Closure>
class ClosureTask : public QueuedTask {
 public:
  explicit ClosureTask(Closure&& closure)
      : mClosure(std::forward<Closure>(closure)) {}

 private:
  bool run() override {
    mClosure();
    return true;
  }
  typename std::decay<Closure>::type mClosure;
};
}  // namespace internal

template <typename Closure>
std::unique_ptr<QueuedTask> ToQueuedTask(Closure&& closure) {
  return std::make_unique<internal::ClosureTask<Closure>>(
      std::forward<Closure>(closure));
}
}  // namespace avp

#endif /* !AVP_TO_QUEUED_TASK_H */
