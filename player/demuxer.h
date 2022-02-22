/*
 * demuxer.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef DEMUXER_H
#define DEMUXER_H

namespace avp {

class Demuxer {
 public:
  Demuxer() = default;
  virtual ~Demuxer() = default;
};
} /* namespace avp */

#endif /* !DEMUXER_H */
