/*
 * null.cc
 * Copyright (C) 2021 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "constructor_magic.h"

class BaseNull {
 public:
  BaseNull() = default;
  virtual ~BaseNull() = default;

 private:
  AVP_DISALLOW_COPY_AND_ASSIGN(BaseNull);
};
