/*
 * media_source.cc
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "media_source.h"

namespace avp {
void MediaSource::ReadOptions::setNonBlocking() {
  mNonBlocking = true;
}

void MediaSource::ReadOptions::clearNonBlocking() {
  mNonBlocking = false;
}

bool MediaSource::ReadOptions::getNonBlocking() const {
  return mNonBlocking;
}

void MediaSource::ReadOptions::setSeekTo(int64_t time_us, SeekMode mode) {
  mOptions |= kSeekTo_Option;
  mSeekTimeUs = time_us;
  mSeekMode = mode;
}

bool MediaSource::ReadOptions::getSeekTo(int64_t* time_us,
                                         SeekMode* mode) const {
  *time_us = mSeekTimeUs;
  *mode = mSeekMode;
  return (mOptions & kSeekTo_Option) != 0;
}

} /* namespace avp */
