/*
 * avplayer.cc
 * Copyright (C) 2021 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */
#include "avplayer.h"

#include "base/checks.h"
#include "base/errors.h"
#include "base/logging.h"
#include "media/foundation/message_object.h"

#include "generic_source.h"

namespace avp {

using ave::media::MessageObject;

namespace {
bool IsHTTPLiveURL(const char* url) {
  if (!strncasecmp("http://", url, 7) || !strncasecmp("https://", url, 8) ||
      !strncasecmp("file://", url, 7)) {
    size_t len = strlen(url);
    if (len >= 5 && !strcasecmp(".m3u8", &url[len - 5])) {
      return true;
    }

    if (strstr(url, "m3u8")) {
      return true;
    }
  }

  return false;
}

bool IsRTSPURL(const char* url) {
  if (!strncasecmp(url, "rtsp://", 7)) {
    return true;
  }
  if (!strncasecmp("http://", url, 7) || !strncasecmp("https://", url, 8) ||
      !strncasecmp("file://", url, 7)) {
    size_t len = strlen(url);
    if (len >= 4 && !strcasecmp(".sdp", &url[len - 4])) {
      return true;
    }

    if (strstr(url, ".sdp?")) {
      return true;
    }
  }

  return false;
}

static bool IsDASHUrl(const char* url) {
  if (!strncasecmp("http://", url, 7) || !strncasecmp("https://", url, 8) ||
      !strncasecmp("file://", url, 7)) {
    size_t len = strlen(url);
    if (len >= 4 && !strcasecmp(".mpd", &url[len - 4])) {
      return true;
    }

    if (strstr(url, ".mpd?")) {
      return true;
    }
  }
  return false;
}

}  // namespace

AvPlayer::AvPlayer(std::shared_ptr<ContentSourceFactory> content_source_factory,
                   std::shared_ptr<DemuxerFactory> demuxer_factory,
                   std::shared_ptr<CodecFactory> codec_factory,
                   std::shared_ptr<AudioDeviceModule> audio_device_module)
    : content_source_factory_(std::move(content_source_factory)),
      demuxer_factory_(std::move(demuxer_factory)),
      codec_factory_(std::move(codec_factory)),
      audio_device_module_(std::move(audio_device_module)),
      player_looper_(std::make_shared<Looper>()),
      media_clock_(std::make_shared<MediaClock>()),
      started_(false),
      prepared_(false),
      paused_(false),
      source_started_(false),
      scan_sources_pending_(false) {
  player_looper_->setName("AvPlayer");
}

AvPlayer::~AvPlayer() {
  player_looper_->unregisterHandler(id());
  player_looper_->stop();
}

status_t AvPlayer::SetListener(std::shared_ptr<Listener> listener) {
  listener_ = std::move(listener);
  return ave::OK;
}

status_t AvPlayer::Init() {
  player_looper_->start();
  player_looper_->registerHandler(shared_from_this());

  return 0;
}

status_t AvPlayer::SetDataSource(
    const char* url,
    const std::unordered_map<std::string, std::string>& headers) {
  std::shared_ptr<ContentSource> source;
  if (IsHTTPLiveURL(url)) {
    // httplive
  } else if (IsRTSPURL(url)) {
    // rtsp
  } else if (IsDASHUrl(url)) {
    // dash
  } else {
    // generic
    std::shared_ptr<GenericSource> generic_source(
        std::make_shared<GenericSource>());
    status_t err = generic_source->SetDataSource(url);
    if (err == ave::OK) {
      source = std::move(generic_source);
    } else {
      AVE_LOG(LS_ERROR) << "setDataSource (" << url << ") error";
    }
  }

  return SetDataSource(source);
}

status_t AvPlayer::SetDataSource(int fd, int64_t offset, int64_t length) {
  std::shared_ptr<ContentSource> source;
  {
    std::shared_ptr<GenericSource> generic_source(
        std::make_shared<GenericSource>());
    generic_source->SetDataSource(fd, offset, length);
    source = std::move(generic_source);
  }

  return SetDataSource(source);
}
status_t AvPlayer::SetDataSource(std::shared_ptr<ave::DataSource> data_source) {
  std::shared_ptr<ContentSource> source;
  return SetDataSource(source);
}

status_t AvPlayer::SetDataSource(std::shared_ptr<ContentSource> source) {
  auto msg = std::make_shared<Message>(kWhatSetDataSource, shared_from_this());
  source->SetNotify(shared_from_this());

  msg->setObject("source", std::static_pointer_cast<MessageObject>(source));
  msg->post();
  return ave::OK;
}

status_t AvPlayer::SetVideoSink(std::shared_ptr<VideoSink> sink) {
  auto msg = std::make_shared<Message>(kWhatSetVideoSink, shared_from_this());
  msg->setObject("videoSink", std::move(sink));
  msg->post();
  return ave::OK;
}

status_t AvPlayer::Prepare() {
  auto msg = std::make_shared<Message>(kWhatPrepare, shared_from_this());
  msg->post();
  return ave::OK;
}

status_t AvPlayer::Start() {
  auto msg = std::make_shared<Message>(kWhatStart, shared_from_this());
  msg->post();
  return ave::OK;
}

status_t AvPlayer::Stop() {
  return ave::OK;
}

status_t AvPlayer::Pause() {
  auto msg = std::make_shared<Message>(kWhatPause, shared_from_this());
  msg->post();
  return ave::OK;
}

status_t AvPlayer::Resume() {
  auto msg = std::make_shared<Message>(kWhatResume, shared_from_this());
  msg->post();
  return ave::OK;
}

status_t AvPlayer::SeekTo(int msec, SeekMode seekMode) {
  return ave::OK;
}

status_t AvPlayer::Reset() {
  auto msg = std::make_shared<Message>(kWhatReset, shared_from_this());
  msg->post(0);
  return ave::OK;
}

///////////////////////////////////////////

void AvPlayer::PostScanSources() {
  if (scan_sources_pending_) {
    return;
  }
  auto msg = std::make_shared<Message>(kWhatScanSources, shared_from_this());
  msg->post();
  scan_sources_pending_ = true;
}

status_t AvPlayer::InstantiateDecoder(bool audio,
                                      std::shared_ptr<AvpDecoder>& decoder) {
  if (decoder.get() != nullptr) {
    return ave::OK;
  }

  auto format = source_->GetTrackInfo(audio);

  if (format.get() == nullptr) {
    return ave::UNKNOWN_ERROR;
  }

  std::string mime = format->mime();
  AVE_LOG(LS_INFO) << "instantiateDecoder mime: " << mime;

  if (audio) {
    auto notify =
        std::make_shared<Message>(kWhatAudioNotify, shared_from_this());
    decoder = std::make_shared<AvpDecoder>(notify, source_, sync_render_);
  } else {
    auto notify =
        std::make_shared<Message>(kWhatVideoNotify, shared_from_this());
    decoder =
        std::make_shared<AvpDecoder>(notify, source_, sync_render_, mVideoSink);
  }

  decoder->Init();

  decoder->Configure(format);

  decoder->Start();

  return ave::OK;
}

///////////////////////////////////////////

void AvPlayer::OnStart(int64_t startUs, SeekMode seekMode) {
  AVE_LOG(LS_INFO) << "onStart";
  if (!source_started_) {
    source_->Start();
    source_started_ = true;
  }
  if (startUs > 0) {
    // TODO(youfa) performSeek
  }

  started_ = true;
  paused_ = true;

  auto renderNotify(
      std::make_shared<Message>(kWhatRendererNotify, shared_from_this()));
  sync_render_ = std::make_shared<AvpRenderSynchronizer>(
      renderNotify, player_looper_, media_clock_);
  sync_render_->init();

  sync_render_->setAudioSink(mAudioSink);
  sync_render_->setVideoSink(mVideoSink);

  PostScanSources();
}

void AvPlayer::OnStop() {}

void AvPlayer::OnPause() {}

void AvPlayer::OnResume() {}

void AvPlayer::OnSeek() {}

void AvPlayer::PerformReset() {
  source_.reset();
}

void AvPlayer::OnSourceNotify(const std::shared_ptr<Message>& msg) {
  int32_t what;
  AVE_CHECK(msg->findInt32("what", &what));
  switch (what) {
    case ContentSource::kWhatPrepared: {
      AVE_LOG(LS_INFO) << "source prepared: " << source_.get();
      if (source_.get() == nullptr) {
        return;
      }
      int32_t err;
      AVE_CHECK(msg->findInt32("err", &err));
      if (err != ave::OK) {
      } else {
        prepared_ = true;
      }

      // TODO(youfa) new msg here
      notifyListner(kWhatPrepared, msg);

      break;
    }
    default:
      break;
  }
}

void AvPlayer::onDecoderNotify(const std::shared_ptr<Message>& msg) {
  int32_t what;
  AVE_CHECK(msg->findInt32("what", &what));
  switch (what) {
    case AvpDecoder::kWhatInputDiscontinuity: {
      break;
    }
    case AvpDecoder::kWhatVideoSizeChanged: {
      break;
    }
    case AvpDecoder::kWhatEOS: {
      break;
    }
    case AvpDecoder::kWhatError: {
      break;
    }
    case AvpDecoder::kWhatFlushCompleted: {
      break;
    }
    case AvpDecoder::kWhatResumeCompleted: {
      break;
    }
    case AvpDecoder::kWhatShutdownCompleted: {
      break;
    }
    default:
      break;
  }
}

void AvPlayer::onRenderNotify(const std::shared_ptr<Message>& msg) {}

void AvPlayer::onMessageReceived(const std::shared_ptr<Message>& message) {
  AVE_LOG(LS_DEBUG) << "AvPlayer::onMessageReceived:" << message->what();
  switch (message->what()) {
      /************* from avplayer ***************/
    case kWhatSetDataSource: {
      std::shared_ptr<MessageObject> obj;
      message->findObject("source", obj);
      source_ = std::dynamic_pointer_cast<ContentSource>(obj);
      notifyListner(kWhatSetDataSourceCompleted, std::make_shared<Message>());
      break;
    }

    case kWhatSetAudioSink: {
      std::shared_ptr<MessageObject> obj;
      message->findObject("audioSink", obj);
      mAudioSink = std::dynamic_pointer_cast<AudioSink>(obj);
      break;
    }

    case kWhatSetVideoSink: {
      std::shared_ptr<MessageObject> obj;
      message->findObject("videoSink", obj);
      mVideoSink = std::dynamic_pointer_cast<VideoSink>(obj);
      if (mVideoDecoder.get() != nullptr) {
        mVideoDecoder->setVideoSink(mVideoSink);
      }
      break;
    }

    case kWhatPrepare: {
      source_->Prepare();
      break;
    }

    case kWhatStart: {
      onStart();
      break;
    }

    case kWhatScanSources: {
      AVE_LOG(LS_INFO) << "kWhatScanSources";
      scan_sources_pending_ = false;

      bool rescan = false;
      if (mVideoSink.get() != nullptr && mVideoDecoder.get() == nullptr) {
        if (instantiateDecoder(false, mVideoDecoder) == WOULD_BLOCK) {
          rescan = true;
        }
      }

      if (mAudioSink.get() != nullptr && mAudioDecoder.get() == nullptr) {
        if (instantiateDecoder(true, mAudioDecoder) == WOULD_BLOCK) {
          rescan = true;
        }
      }

      if (rescan) {
        message->post(1000000LL);
        scan_sources_pending_ = true;
      }
      break;
    }

    case kWhatSeek: {
      break;
    }

    case kWhatPause: {
      break;
    }

    case kWhatResume: {
      break;
    }

    case kWhatReset: {
      performReset();
    } break;

    case kWhatSourceNotify: {
      onSourceNotify(message);
      break;
    }
    case kWhatAudioNotify:
    case kWhatVideoNotify: {
      onDecoderNotify(message);
      break;
    }

    case kWhatRendererNotify: {
      onRenderNotify(message);
      break;
    }
    default:
      break;
  }
}

}  // namespace avp
