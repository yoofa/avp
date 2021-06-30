/*
 * constructor_magic.h
 * Copyright (C) 2021 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AVP_CONSTRUCTOR_MAGIC_H
#define AVP_CONSTRUCTOR_MAGIC_H

#define AVP_DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(const TypeName&) = delete;          \
  TypeName& operator=(const TypeName&) = delete

#endif /* !AVP_CONSTRUCTOR_MAGIC_H */
