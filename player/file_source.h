/*
 * file_source.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef FILE_SOURCE_H
#define FILE_SOURCE_H

#include <mutex>
#include <string>

#include "player/data_source.h"

namespace avp {

class FileSource : public DataSource {
 public:
  FileSource(const char* filename);
  FileSource(int fd, int64_t offset, int64_t length);
  virtual ~FileSource() override;

  virtual status_t initCheck() const override;

  virtual ssize_t read(void* data, size_t size) override;

  virtual ssize_t readAt(off64_t offset, void* data, size_t size) override;

  virtual status_t getPosition(off64_t* position) override;

  virtual ssize_t seek(off64_t position, int whence) override;

  virtual status_t getSize(off64_t* size) override;

  virtual uint32_t flags() override { return kIsLocalFileSource | kSeekable; }

  virtual std::string toString() { return mName; }

 protected:
  ssize_t read_l(void* data, size_t size);
  ssize_t seek_l(off64_t position, int whence);
  ssize_t readAt_l(off64_t offset, void* data, size_t size);

  int mFd;
  int64_t mStartOffset;
  int64_t mLength;
  int64_t mOffset;
  std::mutex mLock;

 private:
  std::string mName;

  FileSource(const FileSource&);
  FileSource& operator=(const FileSource&);
};

} /* namespace avp */

#endif /* !FILE_SOURCE_H */
