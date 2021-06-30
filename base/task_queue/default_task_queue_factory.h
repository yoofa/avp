/*
 * default_task_queue_factory.h
 * Copyright (C) 2021 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AVP_DEFAULT_TASK_QUEUE_FACTORY_H
#define AVP_DEFAULT_TASK_QUEUE_FACTORY_H

#include <memory>

#include "task_queue_factory.h"

namespace avp {

std::unique_ptr<TaskQueueFactory> CreateDefaultTaskQueueFactory();

}  // namespace avp

#endif /* !AVP_DEFAULT_TASK_QUEUE_FACTORY_H */
