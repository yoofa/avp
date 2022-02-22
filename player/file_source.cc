/*
 * file_source.cc
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "file_source.h"

#include <string>

#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "base/logging.h"
#include "base/utils.h"

namespace avp {

FileSource::FileSource(const char* filename)
    : mFd(-1), mOffset(0), mLength(-1), mName("<null>") {
  if (filename) {
    mName = std::string("FileSource(") + filename + ")";
  }
  LOG(LS_VERBOSE) << mName;
  mFd = open(filename, O_LARGEFILE | O_RDONLY);

  if (mFd >= 0) {
    mLength = lseek64(mFd, 0, SEEK_END);
  } else {
    LOG(LS_ERROR) << "Failed to open file" << filename << ". "
                  << strerror(errno);
  }
}

FileSource::FileSource(int fd, int64_t offset, int64_t length)
    : mFd(fd), mOffset(offset), mLength(length), mName("<null>") {
  LOG(LS_VERBOSE) << "fd=" << fd << ", offset=" << offset
                  << ", length=" << length;

  if (mOffset < 0) {
    mOffset = 0;
  }
  if (mLength < 0) {
    mLength = 0;
  }
  if (mLength > INT64_MAX - mOffset) {
    mLength = INT64_MAX - mOffset;
  }
  struct stat s;
  if (fstat(fd, &s) == 0) {
    if (mOffset > s.st_size) {
      mOffset = s.st_size;
      mLength = 0;
    }
    if (mOffset + mLength > s.st_size) {
      mLength = s.st_size - mOffset;
    }
  }
  if (mOffset != offset || mLength != length) {
    LOG(LS_WARING) << "offset/length adjusted from" << offset << "/" << length
                   << " to " << mOffset << "/" << mLength;
  }

  mName = std::string("FileSource(fd(") + nameForFd(fd).c_str() + "), " +
          std::to_string(mOffset) + ", " + std::to_string(mLength) + ")";
}

FileSource::~FileSource() {
  if (mFd >= 0) {
    ::close(mFd);
    mFd = -1;
  }
}

status_t FileSource::initCheck() const {
  return mFd >= 0 ? 0 : -1;
}

ssize_t FileSource::readAt(off64_t offset, void* data, size_t size) {
  if (mFd < 0) {
    return -1;
  }
  std::lock_guard<std::mutex> lock(mLock);

  if (mLength >= 0) {
    if (offset < 0) {
      return -1;
    }
    if (offset >= mLength) {
      return 0;
    }
    uint64_t numAvailable = mLength - offset;
    if ((uint64_t)size > numAvailable) {
      size = numAvailable;
    }
  }
  return readAt_l(offset, data, size);
}

ssize_t FileSource::readAt_l(off64_t offset, void* data, size_t size) {
  off64_t result = lseek64(mFd, offset + mOffset, SEEK_SET);
  if (result == -1) {
    LOG(LS_ERROR) << "seek to " << (offset + mOffset) << "failed";
    return -1;
  }

  return ::read(mFd, data, size);
}

status_t FileSource::getSize(off64_t* size) {
  std::lock_guard<std::mutex> lock(mLock);

  if (mFd < 0) {
    return -1;
  }

  *size = mLength;

  return 0;
}
} /* namespace avp */
