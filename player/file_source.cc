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
    : mFd(-1), mStartOffset(0), mLength(-1), mName("<null>") {
  if (filename) {
    mName = std::string("FileSource(") + filename + ")";
  }
  AVE_LOG(LS_VERBOSE) << mName;
  mFd = open(filename, O_LARGEFILE | O_RDONLY);

  if (mFd >= 0) {
    mLength = lseek64(mFd, 0, SEEK_END);
  } else {
    AVE_LOG(LS_ERROR) << "Failed to open file" << filename << ". "
                      << strerror(errno);
  }
}

FileSource::FileSource(int fd, int64_t offset, int64_t length)
    : mFd(fd), mStartOffset(offset), mLength(length), mName("<null>") {
  AVE_LOG(LS_VERBOSE) << "fd=" << fd << ", offset=" << offset
                      << ", length=" << length;

  if (mStartOffset < 0) {
    mStartOffset = 0;
  }
  if (mLength < 0) {
    mLength = 0;
  }
  if (mLength > INT64_MAX - mStartOffset) {
    mLength = INT64_MAX - mStartOffset;
  }
  struct stat s;
  if (fstat(fd, &s) == 0) {
    if (mStartOffset > s.st_size) {
      mStartOffset = s.st_size;
      mLength = 0;
    }
    if (mStartOffset + mLength > s.st_size) {
      mLength = s.st_size - mStartOffset;
    }
  }
  if (mStartOffset != offset || mLength != length) {
    AVE_LOG(LS_WARNING) << "offset/length adjusted from" << offset << "/"
                        << length << " to " << mStartOffset << "/" << mLength;
  }

  mName = std::string("FileSource(fd(") + nameForFd(fd).c_str() + "), " +
          std::to_string(mStartOffset) + ", " + std::to_string(mLength) + ")";
}

FileSource::~FileSource() {
  if (mFd >= 0) {
    ::close(mFd);
    mFd = -1;
  }
}

status_t FileSource::initCheck() const {
  return mFd >= 0 ? OK : UNKNOWN_ERROR;
}

status_t FileSource::getPosition(off64_t* position) {
  std::lock_guard<std::mutex> lock(mLock);
  *position = mOffset;
  return OK;
}

ssize_t FileSource::seek(off64_t position, int whence) {
  if (mFd < 0) {
    return NO_INIT;
  }

  if (position < 0) {
    return UNKNOWN_ERROR;
  }

  if (position > mLength) {
    return OK;
  }

  std::lock_guard<std::mutex> lock(mLock);
  return seek_l(position, whence);
}

ssize_t FileSource::seek_l(off64_t position, int whence) {
  off64_t result = lseek64(mFd, position + mStartOffset, SEEK_SET);
  if (result >= 0) {
    mOffset = result;
  }
  return mOffset;
}

ssize_t FileSource::read_l(void* data, size_t size) {
  uint64_t sizeToRead = size;
  if (static_cast<int64_t>(mOffset + size) > mLength) {
    sizeToRead = mLength - mOffset;
  }
  ssize_t readSize = ::read(mFd, data, sizeToRead);
  if (readSize > 0) {
    mOffset += readSize;
  }
  return readSize;
}

ssize_t FileSource::read(void* data, size_t size) {
  if (mFd < 0) {
    return NO_INIT;
  }

  std::lock_guard<std::mutex> lock(mLock);
  return read_l(data, size);
}

ssize_t FileSource::readAt(off64_t offset, void* data, size_t size) {
  if (mFd < 0) {
    return NO_INIT;
  }
  std::lock_guard<std::mutex> lock(mLock);

  ssize_t seekOffset = seek_l(offset, SEEK_SET);

  if (seekOffset < 0) {
    return seekOffset;
  }

  return read_l(data, size);
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
