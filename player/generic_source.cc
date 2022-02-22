/*
 * generic_source.cc
 * Copyright (C) 2021 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "generic_source.h"

#include <memory>

#include <string.h>

#include "base/logging.h"
#include "base/unique_fd.h"
#include "common/looper.h"
#include "common/message.h"
#ifdef AVP_FFMPEG_DEMUXER
#include "demuxer/ffmpeg_demuxer_factory.h"
#endif
#include "player/default_demuxer_factory.h"
#include "player/file_source.h"

namespace avp {

GenericSource::GenericSource()
    :
#ifdef AVP_FFMPEG_DEMUXER
      mDemuxerFactory(std::make_unique<FFmpegDemuxerFactory>()),
#else
      mDemuxerFactory(std::make_unique<DefaultDemuxerFactory>()),
#endif
      mLooper(std::make_shared<Looper>()) {
  mLooper->setName("generic source");
}

GenericSource::~GenericSource() {}

void GenericSource::resetDataSource() {}

status_t GenericSource::setDataSource(const char* url) {
  std::lock_guard<std::mutex> lock(mLock);
  resetDataSource();

  mUri = url;

  return 0;
}
status_t GenericSource::setDataSource(int fd, int64_t offset, int64_t length) {
  std::lock_guard<std::mutex> lock(mLock);
  resetDataSource();

  mFd.reset(dup(fd));
  mOffset = offset;
  mLength = length;

  return 0;
}

void GenericSource::prepare() {
  mLooper->registerHandler(shared_from_this());
  mLooper->start();

  auto msg = std::make_shared<Message>(kWhatPrepare, shared_from_this());
  msg->post(0);
}

void GenericSource::start() {}

void GenericSource::stop() {}

void GenericSource::pause() {}

void GenericSource::resume() {}

status_t GenericSource::dequeueAccussUnit(bool audio) {
  return OK;
}

size_t GenericSource::getTrackCount() const {
  return 0;
}

std::shared_ptr<Message> GenericSource::getTrackInfo(size_t trackIndex) const {
  return nullptr;
}

status_t GenericSource::selectTrack(size_t trackIndex, bool select) const {
  return 0;
}

/***************************************/
status_t GenericSource::initFromDataSource() {
  LOG(LS_INFO) << "GenericSource::initFromDataSource";
  return OK;
}

void GenericSource::onPrepare() {
  // create DataSource
  if (mDataSource.get() == nullptr) {
    if (!mUri.empty()) {
      const char* uri = mUri.c_str();
      if (!strncasecmp("file://", uri, 7)) {
        mDataSource = std::make_shared<FileSource>(mUri.substr(7).c_str());
      }
    } else {
      mDataSource =
          std::make_shared<FileSource>(dup(mFd.get()), mOffset, mLength);
    }
  }

  // probe source and init demuxer
  status_t err = initFromDataSource();
  if (err != OK) {
    return;
  }

  // notify prepared
  std::shared_ptr<Message> notify = dupNotify();
  std::shared_ptr<Message> msg(std::make_shared<Message>());
  msg->setWhat(kWhatPrepared);
  notify->setMessage("notify", msg);
  notify->post(0);
}

void GenericSource::onMessageReceived(const std::shared_ptr<Message>& message) {
  switch (message->what()) {
    case kWhatPrepare: {
      onPrepare();
      break;
    }
  }
}

}  // namespace avp
