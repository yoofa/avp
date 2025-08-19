/*
 * AudioFileRender.cc
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "AudioFileRender.h"

#include <string>

#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "base/logging.h"
#include "base/utils.h"
#include "media/foundation/buffer.h"

namespace ave {
namespace media {

AudioFileRender::AudioFileRender(const char* file) : mFd(-1) {
  mFd = ::open(file, O_LARGEFILE | O_RDWR | O_CREAT);

  if (mFd >= 0) {
    lseek64(mFd, 0, SEEK_END);
  } else {
    AVE_LOG(LS_ERROR) << "Failed to open file" << file << ". "
                      << strerror(errno);
  }
}

AudioFileRender::~AudioFileRender() {
  if (mFd > 0) {
    ::close(mFd);
    mFd = -1;
  }
}

void AudioFileRender::onFrame(std::shared_ptr<Buffer>& frame) {
  // AVE_LOG(LS_INFO) << "onFrame, size:" << frame->size();
  ::write(mFd, frame->data(), frame->size());
}

}  // namespace media
}  // namespace ave
