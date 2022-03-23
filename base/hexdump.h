/*
 * hexdump.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef HEXDUMP_H
#define HEXDUMP_H

#include "base/types.h"

namespace avp {

void hexdump(const void* data, size_t size, size_t indent = 0);

} /* namespace avp */

#endif /* !HEXDUMP_H */
