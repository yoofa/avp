/*
 * media_clock.cc
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "media_clock.h"

#include <map>

#include <base/checks.h>
#include <base/logging.h>
#include <common/message.h>

namespace avp {

// Maximum allowed time backwards from anchor change.
// If larger than this threshold, it's treated as discontinuity.
static const int64_t kAnchorFluctuationAllowedUs = 10000LL;

MediaClock::Timer::Timer(const std::shared_ptr<Message>& notify,
                         int64_t mediaTimeUs,
                         int64_t adjustRealUs)
    : mNotify(notify), mMediaTimeUs(mediaTimeUs), mAdjustRealUs(adjustRealUs) {}

MediaClock::MediaClock()
    : mLooper(std::make_shared<Looper>()),
      mAnchorTimeMediaUs(-1),
      mAnchorTimeRealUs(-1),
      mMaxTimeMediaUs(INT64_MAX),
      mStartingTimeMediaUs(-1),
      mPlaybackRate(1.0),
      mGeneration(0) {
  mLooper->setName("MediaClock");
  mLooper->start();
}

void MediaClock::init() {
  mLooper->registerHandler(shared_from_this());
}

MediaClock::~MediaClock() {
  reset();
  if (mLooper != NULL) {
    mLooper->unregisterHandler(id());
    mLooper->stop();
  }
}

void MediaClock::reset() {
  std::lock_guard<std::mutex> l(mLock);
  auto it = mTimers.begin();
  while (it != mTimers.end()) {
    it->mNotify->setInt32("reason", TIMER_REASON_RESET);
    it->mNotify->post();
    it = mTimers.erase(it);
  }
  mMaxTimeMediaUs = INT64_MAX;
  mStartingTimeMediaUs = -1;
  updateAnchorTimesAndPlaybackRate_l(-1, -1, 1.0);
  ++mGeneration;
}

void MediaClock::setStartingTimeMedia(int64_t startingTimeMediaUs) {
  std::lock_guard<std::mutex> l(mLock);
  mStartingTimeMediaUs = startingTimeMediaUs;
}

void MediaClock::clearAnchor() {
  std::lock_guard<std::mutex> l(mLock);
  updateAnchorTimesAndPlaybackRate_l(-1, -1, mPlaybackRate);
}

void MediaClock::updateAnchor(int64_t anchorTimeMediaUs,
                              int64_t anchorTimeRealUs,
                              int64_t maxTimeMediaUs) {
  if (anchorTimeMediaUs < 0 || anchorTimeRealUs < 0) {
    LOG(LS_WARNING) << "reject anchor time since it is negative.";
    return;
  }

  std::lock_guard<std::mutex> l(mLock);
  int64_t nowUs = Looper::getNowUs();
  int64_t nowMediaUs =
      anchorTimeMediaUs + (nowUs - anchorTimeRealUs) * (double)mPlaybackRate;
  if (nowMediaUs < 0) {
    LOG(LS_WARNING)
        << "reject anchor time since it leads to negative media time.";
    return;
  }

  if (maxTimeMediaUs != -1) {
    mMaxTimeMediaUs = maxTimeMediaUs;
  }
  if (mAnchorTimeRealUs != -1) {
    int64_t oldNowMediaUs = mAnchorTimeMediaUs +
                            (nowUs - mAnchorTimeRealUs) * (double)mPlaybackRate;
    if (nowMediaUs < oldNowMediaUs + kAnchorFluctuationAllowedUs &&
        nowMediaUs > oldNowMediaUs - kAnchorFluctuationAllowedUs) {
      return;
    }
  }
  updateAnchorTimesAndPlaybackRate_l(nowMediaUs, nowUs, mPlaybackRate);

  ++mGeneration;
  processTimers_l();
}

void MediaClock::updateMaxTimeMedia(int64_t maxTimeMediaUs) {
  std::lock_guard<std::mutex> l(mLock);
  mMaxTimeMediaUs = maxTimeMediaUs;
}

void MediaClock::setPlaybackRate(float rate) {
  CHECK_GE(rate, 0.0);
  std::lock_guard<std::mutex> l(mLock);
  if (mAnchorTimeRealUs == -1) {
    mPlaybackRate = rate;
    return;
  }

  int64_t nowUs = Looper::getNowUs();
  int64_t nowMediaUs =
      mAnchorTimeMediaUs + (nowUs - mAnchorTimeRealUs) * (double)mPlaybackRate;
  if (nowMediaUs < 0) {
    LOG(LS_WARNING) << "setRate: anchor time should not be negative, set to 0.";
    nowMediaUs = 0;
  }
  updateAnchorTimesAndPlaybackRate_l(nowMediaUs, nowUs, rate);

  if (rate > 0.0) {
    ++mGeneration;
    processTimers_l();
  }
}

float MediaClock::getPlaybackRate() const {
  std::lock_guard<std::mutex> l(mLock);
  return mPlaybackRate;
}

void MediaClock::setFreeRun(bool free_run) {
  std::lock_guard<std::mutex> l(mLock);
  free_run_ = free_run;
}

status_t MediaClock::getMediaTime(int64_t realUs,
                                  int64_t* outMediaUs,
                                  bool allowPastMaxTime) const {
  if (outMediaUs == NULL) {
    return BAD_VALUE;
  }

  std::lock_guard<std::mutex> l(mLock);
  return getMediaTime_l(realUs, outMediaUs, allowPastMaxTime);
}

status_t MediaClock::getMediaTime_l(int64_t realUs,
                                    int64_t* outMediaUs,
                                    bool allowPastMaxTime) const {
  if (mAnchorTimeRealUs == -1) {
    return NO_INIT;
  }

  int64_t mediaUs =
      mAnchorTimeMediaUs + (realUs - mAnchorTimeRealUs) * (double)mPlaybackRate;

  if (mediaUs > mMaxTimeMediaUs && !allowPastMaxTime) {
    mediaUs = mMaxTimeMediaUs;
  }
  if (mediaUs < mStartingTimeMediaUs) {
    mediaUs = mStartingTimeMediaUs;
  }
  if (mediaUs < 0) {
    mediaUs = 0;
  }
  *outMediaUs = mediaUs;
  return OK;
}

status_t MediaClock::getRealTimeFor(int64_t targetMediaUs,
                                    int64_t* outRealUs) const {
  if (outRealUs == NULL) {
    return BAD_VALUE;
  }

  std::lock_guard<std::mutex> l(mLock);
  if (mPlaybackRate == 0.0) {
    return NO_INIT;
  }

  int64_t nowUs = Looper::getNowUs();
  int64_t nowMediaUs;
  status_t status =
      getMediaTime_l(nowUs, &nowMediaUs, true /* allowPastMaxTime */);
  if (status != OK) {
    return status;
  }
  *outRealUs = (targetMediaUs - nowMediaUs) / (double)mPlaybackRate + nowUs;
  return OK;
}

void MediaClock::addTimer(const std::shared_ptr<Message>& notify,
                          int64_t mediaTimeUs,
                          int64_t adjustRealUs) {
  std::lock_guard<std::mutex> l(mLock);

  bool updateTimer = (mPlaybackRate != 0.0);
  if (updateTimer) {
    auto it = mTimers.begin();
    while (it != mTimers.end()) {
      if (((it->mAdjustRealUs - (double)adjustRealUs) * (double)mPlaybackRate +
           (it->mMediaTimeUs - mediaTimeUs)) <= 0) {
        updateTimer = false;
        break;
      }
      ++it;
    }
  }

  mTimers.emplace_back(notify, mediaTimeUs, adjustRealUs);

  if (updateTimer) {
    ++mGeneration;
    processTimers_l();
  }
}

void MediaClock::onMessageReceived(const std::shared_ptr<Message>& msg) {
  switch (msg->what()) {
    case kWhatTimeIsUp: {
      int32_t generation;
      CHECK(msg->findInt32("generation", &generation));

      std::lock_guard<std::mutex> l(mLock);
      if (generation != mGeneration) {
        break;
      }
      processTimers_l();
      break;
    }

    default:
      break;
  }
}

void MediaClock::processTimers_l() {
  int64_t nowMediaTimeUs;
  status_t status = getMediaTime_l(Looper::getNowUs(), &nowMediaTimeUs,
                                   free_run_ /* allowPastMaxTime */);

  if (status != OK) {
    return;
  }

  int64_t nextLapseRealUs = INT64_MAX;
  std::multimap<int64_t, Timer> notifyList;
  auto it = mTimers.begin();
  while (it != mTimers.end()) {
    double diff = it->mAdjustRealUs * (double)mPlaybackRate + it->mMediaTimeUs -
                  nowMediaTimeUs;
    int64_t diffMediaUs;
    if (diff > (double)INT64_MAX) {
      diffMediaUs = INT64_MAX;
    } else if (diff < (double)INT64_MIN) {
      diffMediaUs = INT64_MIN;
    } else {
      diffMediaUs = diff;
    }

    if (diffMediaUs <= 0) {
      notifyList.emplace(diffMediaUs, *it);
      it = mTimers.erase(it);
    } else {
      if (mPlaybackRate != 0.0 &&
          (double)diffMediaUs < INT64_MAX * (double)mPlaybackRate) {
        int64_t targetRealUs = diffMediaUs / (double)mPlaybackRate;
        if (targetRealUs < nextLapseRealUs) {
          nextLapseRealUs = targetRealUs;
        }
      }
      ++it;
    }
  }

  auto itNotify = notifyList.begin();
  while (itNotify != notifyList.end()) {
    itNotify->second.mNotify->setInt32("reason", TIMER_REASON_REACHED);
    itNotify->second.mNotify->post();
    itNotify = notifyList.erase(itNotify);
  }

  if (mTimers.empty() || mPlaybackRate == 0.0 || mAnchorTimeMediaUs < 0 ||
      nextLapseRealUs == INT64_MAX) {
    return;
  }

  auto msg = std::make_shared<Message>(kWhatTimeIsUp, shared_from_this());
  msg->setInt32("generation", mGeneration);
  msg->post(nextLapseRealUs);
}

void MediaClock::updateAnchorTimesAndPlaybackRate_l(int64_t anchorTimeMediaUs,
                                                    int64_t anchorTimeRealUs,
                                                    float playbackRate) {
  if (mAnchorTimeMediaUs != anchorTimeMediaUs ||
      mAnchorTimeRealUs != anchorTimeRealUs || mPlaybackRate != playbackRate) {
    mAnchorTimeMediaUs = anchorTimeMediaUs;
    mAnchorTimeRealUs = anchorTimeRealUs;
    mPlaybackRate = playbackRate;
    notifyDiscontinuity_l();
  }
}

void MediaClock::setNotificationMessage(const std::shared_ptr<Message>& msg) {
  std::lock_guard<std::mutex> l(mLock);
  mNotify = msg;
}

void MediaClock::notifyDiscontinuity_l() {
  if (mNotify != nullptr) {
    std::shared_ptr<Message> msg = mNotify->dup();
    msg->setInt64("anchor-media-us", mAnchorTimeMediaUs);
    msg->setInt64("anchor-real-us", mAnchorTimeRealUs);
    msg->setFloat("playback-rate", mPlaybackRate);
    msg->post();
  }
}

}  // namespace avp
