/*
 * generic_source.cc
 * Copyright (C) 2021 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */
#include "generic_source.h"

#include <memory>

#include <string.h>

#include "base/checks.h"
#include "base/errors.h"
#include "base/logging.h"
#include "base/unique_fd.h"
#include "media/looper.h"
#include "media/message.h"
#ifdef AVP_FFMPEG_DEMUXER
#include "demuxer/ffmpeg_demuxer_factory.h"
#endif
#include "media/meta_data.h"
#include "player/default_demuxer_factory.h"
#include "player/file_source.h"

namespace avp {

GenericSource::GenericSource()
    : mPendingReadBufferTypes(0),
#ifdef AVP_FFMPEG_DEMUXER
      mDemuxerFactory(std::make_unique<FFmpegDemuxerFactory>()),
#else
      mDemuxerFactory(std::make_unique<DefaultDemuxerFactory>()),
#endif
      mDurationUs(-1LL),
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
  LOG(LS_VERBOSE) << "setDataSource";
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
  msg->post();
}

void GenericSource::start() {}

void GenericSource::stop() {}

void GenericSource::pause() {}

void GenericSource::resume() {}

status_t GenericSource::seekTo(int64_t seekTimeUs, SeekMode mode) {
  LOG(LS_VERBOSE) << "seekTo: " << seekTimeUs << ", mode: " << mode;
  auto msg = std::make_shared<Message>(kWhatSeek, shared_from_this());
  msg->setInt64("seekTimeUs", seekTimeUs);
  msg->setInt32("mode", mode);

  std::shared_ptr<Message> response;
  status_t err = msg->postAndWaitResponse(response);
  if (err == OK && response.get() != nullptr) {
    CHECK(response->findInt32("err", &err));
  }
  return err;
}

std::shared_ptr<MetaData> GenericSource::getSourceMeta() {
  return nullptr;
}

std::shared_ptr<MetaData> GenericSource::getMeta(bool audio) {
  std::lock_guard<std::mutex> lock(mLock);
  std::shared_ptr<MediaSource> source =
      audio ? mAudioTrack.mSource : mVideoTrack.mSource;
  if (source.get() == nullptr) {
    return nullptr;
  }
  return source->getMeta();
}

status_t GenericSource::dequeueAccessUnit(bool audio,
                                          std::shared_ptr<Buffer>& accessUnit) {
  std::lock_guard<std::mutex> lock(mLock);

  Track* track = audio ? &mAudioTrack : &mVideoTrack;
  if (track->mSource.get() == nullptr) {
    return -EWOULDBLOCK;
  }
  status_t result;

  if (!track->mPacketSource->hasBufferAvailable(&result)) {
    if (result == OK) {
      postReadBuffer(audio ? PlayerBase::MEDIA_TRACK_TYPE_AUDIO
                           : PlayerBase::MEDIA_TRACK_TYPE_VIDEO);
      return WOULD_BLOCK;
    }
    return result;
  }

  result = track->mPacketSource->dequeueAccessUnit(accessUnit);

  // TODO(youfa) judge to read more data here.

  if (result != OK) {
    return result;
  }

  int64_t timeUs;
  CHECK(accessUnit->meta()->findInt64("timeUs", &timeUs));

  // TODO(youfa) fetch subtitle data with timeUs

  return result;
}

status_t GenericSource::getDuration(int64_t* durationUs) {
  std::lock_guard<std::mutex> lock(mLock);
  *durationUs = mDurationUs;
  return OK;
}

size_t GenericSource::getTrackCount() const {
  std::lock_guard<std::mutex> lock(mLock);
  return mSources.size();
}

std::shared_ptr<Message> GenericSource::getTrackInfo(size_t trackIndex) const {
  std::lock_guard<std::mutex> lock(mLock);
  return nullptr;
}

status_t GenericSource::selectTrack(size_t trackIndex, bool select) const {
  std::lock_guard<std::mutex> lock(mLock);
  return OK;
}

/***************************************/
status_t GenericSource::initFromDataSource() {
  LOG(LS_INFO) << "GenericSource::initFromDataSource";

  std::shared_ptr<DataSource> datasource;
  datasource = mDataSource;

  CHECK(datasource.get() != nullptr);

  mLock.unlock();
  mDemuxer = mDemuxerFactory->createDemuxer(datasource);
  mLock.lock();

  if (!mDemuxer.get()) {
    return UNKNOWN_ERROR;
  }

  std::shared_ptr<MetaData> sourceMeta;
  mDemuxer->getDemuxerMeta(sourceMeta);
  mSourceMeta = sourceMeta;

  if (mSourceMeta.get() != nullptr) {
    int64_t duration;
    if (mSourceMeta->findInt64(kKeyDuration, &duration)) {
      mDurationUs = duration;
    }
  }

  size_t numTracks = mDemuxer->getTrackCount();
  if (numTracks == 0) {
    LOG(LS_ERROR) << "initFromDataSource, source has no track!";
    return UNKNOWN_ERROR;
  }

  int32_t totalBitrate = 0;

  for (size_t i = 0; i < numTracks; i++) {
    std::shared_ptr<MediaSource> track = mDemuxer->getTrack(i);
    if (!track.get()) {
      continue;
    }

    std::shared_ptr<MetaData> meta;
    mDemuxer->getTrackMeta(meta, i);
    if (!meta.get()) {
      LOG(LS_ERROR) << "no metadata for track " << i;
      return UNKNOWN_ERROR;
    }

    const char* mime;
    CHECK(meta->findCString(kKeyMIMEType, &mime));
    LOG(LS_VERBOSE) << "initFromDataSource track[" << i << "]: " << mime;

    if (!strncasecmp(mime, "audio/", 6) && !mAudioTrack.mSource.get()) {
      mAudioTrack.mIndex = i;
      mAudioTrack.mSource = track;
      mAudioTrack.mPacketSource = std::make_shared<PacketSource>();
    }
    if (!strncasecmp(mime, "video/", 6) && !mVideoTrack.mSource.get()) {
      mVideoTrack.mIndex = i;
      mVideoTrack.mSource = track;
      mVideoTrack.mPacketSource = std::make_shared<PacketSource>();
    }

    mSources.push_back(track);
    int64_t durationUs;
    if (meta->findInt64(kKeyDuration, &durationUs) &&
        (durationUs > mDurationUs)) {
      mDurationUs = durationUs;
    }

    int32_t bitrate;
    if (totalBitrate >= 0 && meta->findInt32(kKeyBitRate, &bitrate)) {
      totalBitrate += bitrate;
    } else {
      totalBitrate = -1;
    }
  }

  mBitrate = totalBitrate;

  LOG(LS_VERBOSE) << "initFromDataSource done. mSources.size: "
                  << mSources.size();

  if (mSources.size() == 0) {
    return UNKNOWN_ERROR;
  }

  return OK;
}

void GenericSource::onPrepare() {
  // create DataSource
  if (mDataSource == nullptr) {
    if (!mUri.empty()) {
      const char* uri = mUri.c_str();
      LOG(LS_INFO) << "onPrepare, uri (" << uri << ")";
      if (!strncasecmp("file://", uri, 7)) {
        auto fileSource = std::make_shared<FileSource>(mUri.substr(7).c_str());
        if (fileSource->initCheck() == OK) {
          mDataSource = std::move(fileSource);
        }
      }
    } else {
      auto fileSource =
          std::make_shared<FileSource>(dup(mFd.get()), mOffset, mLength);
      if (fileSource->initCheck() == OK) {
        mDataSource = std::move(fileSource);
      }
    }

    if (mDataSource == nullptr) {
      LOG(LS_ERROR) << "Failed to create DataSource";
      notifyPreparedAndCleanup(UNKNOWN_ERROR);
      return;
    }
  }

  // probe source and init demuxer
  status_t err = initFromDataSource();
  if (err != OK) {
    notifyPreparedAndCleanup(err);
    return;
  }

  finishPrepare();
}

void GenericSource::finishPrepare() {
  LOG(LS_VERBOSE) << "finishPrepare";
  status_t err = startSources();
  if (err != OK) {
    notifyPreparedAndCleanup(err);
    return;
  }

  // TODO(yofua) when streaming, notify until buffering done
  notifyPrepared();

  if (mAudioTrack.mSource.get() != nullptr) {
    postReadBuffer(PlayerBase::MEDIA_TRACK_TYPE_AUDIO);
  }

  if (mVideoTrack.mSource.get() != nullptr) {
    postReadBuffer(PlayerBase::MEDIA_TRACK_TYPE_VIDEO);
  }
}

status_t GenericSource::startSources() {
  if (mAudioTrack.mSource.get() != nullptr &&
      mAudioTrack.mSource->start() != OK) {
    LOG(LS_ERROR) << "failed to start audio track!";
    return UNKNOWN_ERROR;
  }

  if (mVideoTrack.mSource.get() != nullptr &&
      mVideoTrack.mSource->start() != OK) {
    LOG(LS_ERROR) << "failed to start video track!";
    return UNKNOWN_ERROR;
  }

  return OK;
}

void GenericSource::notifyPreparedAndCleanup(status_t err) {
  if (err != OK) {
    // TODO(youfa) reset data source
  }
  notifyPrepared(err);
}

// with Lock
void GenericSource::postReadBuffer(media_track_type trackType) {
  if ((mPendingReadBufferTypes & (1 << trackType)) == 0) {
    mPendingReadBufferTypes |= (1 << trackType);
    std::shared_ptr<Message> msg =
        std::make_shared<Message>(kWhatReadBuffer, shared_from_this());
    msg->setInt32("trackType", trackType);
    msg->post(0);
  }
}

void GenericSource::onReadBuffer(const std::shared_ptr<Message>& msg) {
  int32_t tmpType;
  CHECK(msg->findInt32("trackType", &tmpType));
  media_track_type trackType = (media_track_type)tmpType;
  mPendingReadBufferTypes &= ~(1 << trackType);
  readBuffer(trackType);
}

void GenericSource::readBuffer(media_track_type trackType,
                               int64_t seekTimeUs,
                               SeekMode seekMode,
                               int64_t* actualTimeUs) {
  Track* track;
  size_t maxBuffers = 1;
  switch (trackType) {
    case PlayerBase::MEDIA_TRACK_TYPE_VIDEO:
      track = &mVideoTrack;
      maxBuffers = 8;  // too large of a number may influence seeks
      break;
    case PlayerBase::MEDIA_TRACK_TYPE_AUDIO:
      track = &mAudioTrack;
      maxBuffers = 64;
      break;
    case PlayerBase::MEDIA_TRACK_TYPE_SUBTITLE:
      track = &mSubtitleTrack;
      break;
    case PlayerBase::MEDIA_TRACK_TYPE_TIMEDTEXT:
      track = &mTimedTextTrack;
      break;
    default:
      return;
      break;
  }

  if (track && track->mSource.get() == nullptr) {
    return;
  }

  if (actualTimeUs) {
    *actualTimeUs = seekTimeUs;
  }

  MediaSource::ReadOptions readOptions;

  if (seekTimeUs >= 0) {
    readOptions.setSeekTo(seekTimeUs, seekMode);
  }

  const bool couldReadMultiple = track->mSource->supportReadMultiple();
  if (couldReadMultiple) {
    readOptions.setNonBlocking();
  }

  for (size_t numBuffer = 0; numBuffer < maxBuffers;) {
    std::vector<std::shared_ptr<Buffer>> mediaBuffers;
    status_t err = OK;

    // will unlock later, add reference
    std::shared_ptr<MediaSource> source = track->mSource;

    //    LOG(LS_INFO) << "before read type:" << trackType;
    mLock.unlock();
    if (couldReadMultiple) {
      err = source->readMultiple(mediaBuffers, maxBuffers - numBuffer,
                                 &readOptions);
    } else {
      std::shared_ptr<Buffer> buffer;
      err = source->read(buffer, &readOptions);
      if (err == OK && buffer.get() != nullptr) {
        mediaBuffers.push_back(buffer);
      }
    }
    mLock.lock();
    //    LOG(LS_INFO) << "after read" << trackType;

    // maybe reset, return;
    if (!track->mPacketSource.get()) {
      return;
    }

    size_t id = 0;
    size_t count = mediaBuffers.size();

    for (; id < count; id++) {
      std::shared_ptr<Buffer> mediaBuffer = mediaBuffers[id];
      track->mPacketSource->queueAccessunit(mediaBuffer);
      numBuffer++;
    }

    if (err == WOULD_BLOCK) {
      break;
    } else if (err != OK) {
      // TODO(youfa)
      break;
    }
  }
}

status_t GenericSource::doSeek(int64_t seekTimeUs, SeekMode mode) {
  if (mVideoTrack.mSource.get()) {
    int64_t actualTimeUs;
    readBuffer(PlayerBase::MEDIA_TRACK_TYPE_VIDEO, seekTimeUs, mode,
               &actualTimeUs);

    if (mode != PlayerBase::SEEK_CLOSEST) {
      seekTimeUs = std::max<int64_t>(0, actualTimeUs);
    }
    mVideoLastDequeueTimeUs = actualTimeUs;
  }

  if (mAudioTrack.mSource.get()) {
    readBuffer(PlayerBase::MEDIA_TRACK_TYPE_AUDIO, seekTimeUs, mode);
    mAudioLastDequeueTimeUs = seekTimeUs;
  }

  return OK;
}

void GenericSource::onMessageReceived(const std::shared_ptr<Message>& msg) {
  std::lock_guard<std::mutex> lock(mLock);
  switch (msg->what()) {
    case kWhatPrepare: {
      onPrepare();
      break;
    }

    case kWhatReadBuffer: {
      onReadBuffer(msg);
      break;
    }

    case kWhatSeek: {
      int64_t seekTimeUs;
      int32_t mode;
      CHECK(msg->findInt64("seekTimeUs", &seekTimeUs));
      CHECK(msg->findInt32("mode", &mode));

      std::shared_ptr<Message> response = std::make_shared<Message>();
      status_t err = doSeek(seekTimeUs, static_cast<SeekMode>(mode));
      response->setInt32("err", err);

      std::shared_ptr<ReplyToken> replyID;
      CHECK(msg->senderAwaitsResponse(replyID));
      response->postReply(replyID);

      break;
    }
  }
}

}  // namespace avp
