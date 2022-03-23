/*
 * VideoFileRender.cc
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "VideoFileRender.h"

#include <string>

#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "base/logging.h"
#include "base/utils.h"
#include "common/buffer.h"

namespace avp {

VideoFileRender::VideoFileRender(const char* file) : mFd(-1) {
  mFd = open(file, O_LARGEFILE | O_RDWR | O_CREAT);

  if (mFd >= 0) {
    lseek64(mFd, 0, SEEK_END);
  } else {
    LOG(LS_ERROR) << "Failed to open file" << file << ". " << strerror(errno);
  }
}

VideoFileRender::~VideoFileRender() {
  if (mFd > 0) {
    ::close(mFd);
    mFd = -1;
  }
}

void VideoFileRender::onFrame(std::shared_ptr<Buffer>& frame) {
  int64_t timeUs;
  frame->meta()->findInt64("timeUs", &timeUs);
  // LOG(LS_INFO) << "onFrame, pts: " << timeUs << ", size:" << frame->size();
  ::write(mFd, frame->data(), frame->size());
}

} /* namespace avp */
