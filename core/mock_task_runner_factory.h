/*
 * mock_task_runner_factory.h
 * Copyright (C) 2024 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef PLAYER_MOCK_TASK_RUNNER_FACTORY_H_
#define PLAYER_MOCK_TASK_RUNNER_FACTORY_H_

#include <gmock/gmock.h>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <queue>
#include <thread>
#include <vector>

#include "base/count_down_latch.h"
#include "base/task_util/task_runner_base.h"
#include "base/task_util/task_runner_factory.h"

namespace ave {
namespace player {

// A mock task runner base that supports virtual time advancing
class MockTaskRunnerBase : public base::TaskRunnerBase {
 public:
  struct ScheduledTask {
    uint64_t due_time_us;
    std::shared_ptr<base::Task> task;

    // Default constructor
    ScheduledTask() : due_time_us(0), task(nullptr) {}

    // Constructor with parameters
    ScheduledTask(uint64_t time, std::unique_ptr<base::Task> t)
        : due_time_us(time), task(std::move(t)) {}

    // Copy constructor (for priority_queue)
    ScheduledTask(const ScheduledTask& other)
        : due_time_us(other.due_time_us), task(other.task) {}

    // Copy assignment operator (for priority_queue)
    ScheduledTask& operator=(const ScheduledTask& other) {
      if (this != &other) {
        due_time_us = other.due_time_us;
        task = other.task;
      }
      return *this;
    }

    // Move constructor
    ScheduledTask(ScheduledTask&& other) noexcept
        : due_time_us(other.due_time_us), task(std::move(other.task)) {}

    // Move assignment operator
    ScheduledTask& operator=(ScheduledTask&& other) noexcept {
      if (this != &other) {
        due_time_us = other.due_time_us;
        task = std::move(other.task);
      }
      return *this;
    }

    // For priority queue: smaller due_time_us has higher priority
    bool operator>(const ScheduledTask& other) const {
      return due_time_us > other.due_time_us;
    }
  };

  MockTaskRunnerBase() : now_us_(0) {}
  ~MockTaskRunnerBase() override = default;

  void Destruct() override {
    // Don't call delete this - the TaskRunner will handle deletion
    // Just clean up any resources if needed
  }

  bool IsCurrent() const {
    // In test environment, always return true to avoid DCHECK failures
    return true;
  }

  void PostTask(std::unique_ptr<base::Task> task) override {
    scheduled_tasks_.push({now_us_, std::move(task)});
  }

  void PostDelayedTask(std::unique_ptr<base::Task> task,
                       uint64_t time_us) override {
    scheduled_tasks_.push({now_us_ + time_us, std::move(task)});
  }

  void PostDelayedTaskAndWait(std::unique_ptr<base::Task> task,
                              uint64_t time_us,
                              bool /*wait*/) override {
    scheduled_tasks_.push({now_us_ + time_us, std::move(task)});
  }

  // Advance virtual time only
  void AdvanceTimeUs(uint64_t delta_us) { now_us_ += delta_us; }

  // Run due tasks in a separate thread
  void RunDueTasks() {
    std::vector<std::shared_ptr<base::Task>> tasks_to_run;

    // Collect all due tasks
    while (!scheduled_tasks_.empty() &&
           scheduled_tasks_.top().due_time_us <= now_us_) {
      auto task = std::move(scheduled_tasks_.top());
      scheduled_tasks_.pop();
      if (task.task) {
        tasks_to_run.push_back(task.task);
      }
    }

    if (tasks_to_run.empty()) {
      return;
    }

    // Execute tasks in a separate thread
    base::CountDownLatch latch(tasks_to_run.size());
    std::thread([&tasks_to_run, &latch]() {
      for (auto& task : tasks_to_run) {
        if (task) {
          task->Run();
        }
        latch.CountDown();
      }
    }).detach();

    // Wait for all tasks to complete
    latch.Wait();
  }

  uint64_t now_us() const { return now_us_; }
  size_t pending_task_count() const { return scheduled_tasks_.size(); }

  void ClearAllTasks() {
    while (!scheduled_tasks_.empty()) {
      scheduled_tasks_.pop();
    }
  }

 private:
  uint64_t now_us_;
  std::priority_queue<ScheduledTask,
                      std::vector<ScheduledTask>,
                      std::greater<ScheduledTask>>
      scheduled_tasks_;
};

// A mock factory that always returns a MockTaskRunnerBase
class MockTaskRunnerFactory : public base::TaskRunnerFactory {
 public:
  MockTaskRunnerFactory() : runner_(nullptr) {}
  ~MockTaskRunnerFactory() override = default;

  std::unique_ptr<base::TaskRunnerBase, base::TaskRunnerDeleter>
  CreateTaskRunner(
      const char* /*name*/,
      base::TaskRunnerFactory::Priority /*priority*/) const override {
    // Create a new runner instance each time to avoid ownership issues
    auto* new_runner = new MockTaskRunnerBase();
    runner_ = new_runner;  // Store the latest runner for test access
    return std::unique_ptr<base::TaskRunnerBase, base::TaskRunnerDeleter>(
        new_runner);
  }

  MockTaskRunnerBase* runner() const { return runner_; }

 private:
  mutable MockTaskRunnerBase* runner_;  // Non-owning pointer for test access
};

}  // namespace player
}  // namespace ave

#endif  // PLAYER_MOCK_TASK_RUNNER_FACTORY_H_