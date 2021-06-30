/*
 * task_queue_std.h
 * Copyright (C) 2021 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AVP_TASK_QUEUE_STD_H
#define AVP_TASK_QUEUE_STD_H

#include <memory>

#include "task_queue_factory.h"

namespace avp {

std::unique_ptr<TaskQueueFactory> createStdTaskQueueFactory();

}  // namespace avp

#endif /* !AVP_TASK_QUEUE_STD_H */
