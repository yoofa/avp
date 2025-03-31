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

#include "content_source/generic_source.h"

#include "message_def.h"

namespace ave {
namespace player {

using ave::media::MessageObject;

AvPlayer::AvPlayer(std::shared_ptr<ContentSourceFactory> content_source_factory,
                   std::shared_ptr<DemuxerFactory> demuxer_factory,
                   std::shared_ptr<CodecFactory> codec_factory,
                   std::shared_ptr<AudioDevice> audio_device)
    : content_source_factory_(std::move(content_source_factory)),
      demuxer_factory_(std::move(demuxer_factory)),
      codec_factory_(std::move(codec_factory)),
      audio_device_(std::move(audio_device)),
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
  auto source = content_source_factory_->CreateContentSource(url, headers);
  if (source == nullptr) {
    return ave::UNKNOWN_ERROR;
  }
  return SetDataSource(source);
}

status_t AvPlayer::SetDataSource(int fd, int64_t offset, int64_t length) {
  auto source =
      content_source_factory_->CreateContentSource(fd, offset, length);
  if (source == nullptr) {
    return ave::UNKNOWN_ERROR;
  }
  return SetDataSource(source);
}
status_t AvPlayer::SetDataSource(std::shared_ptr<ave::DataSource> data_source) {
  auto source =
      content_source_factory_->CreateContentSource(std::move(data_source));
  if (source == nullptr) {
    return ave::UNKNOWN_ERROR;
  }
  return SetDataSource(source);
}

status_t AvPlayer::SetDataSource(std::shared_ptr<ContentSource> source) {
  auto msg = std::make_shared<Message>(kWhatSetDataSource, shared_from_this());
  source->SetNotify(this);

  msg->setObject(kContentSource,
                 std::static_pointer_cast<MessageObject>(std::move(source)));
  msg->post();
  return ave::OK;
}

status_t AvPlayer::SetVideoRender(std::shared_ptr<VideoRender> video_render) {
  auto msg = std::make_shared<Message>(kWhatSetVideoRender, shared_from_this());
  msg->setObject(kVideoRender, std::move(video_render));
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
  auto msg = std::make_shared<Message>(kWhatStop, shared_from_this());
  msg->post();
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
  auto msg = std::make_shared<Message>(kWhatSeek, shared_from_this());
  msg->setInt64(kSeekToUs, msec * 1000LL);
  msg->setInt32(kSeekMode, static_cast<int32_t>(seekMode));
  msg->post();
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
                                      std::shared_ptr<AVPDecoder>& decoder) {
  if (decoder != nullptr) {
    return ave::OK;
  }

  auto format = source_->GetTrackInfo(audio);

  if (format == nullptr) {
    return ave::UNKNOWN_ERROR;
  }

  std::string mime = format->mime();
  AVE_LOG(LS_INFO) << "instantiateDecoder mime: " << mime;

  if (audio) {
    auto notify =
        std::make_shared<Message>(kWhatAudioNotify, shared_from_this());
    audio_render_ = std::make_shared<AVPAudioRender>(
        nullptr, sync_controller_.get(), audio_device_, true);
    decoder = std::make_shared<AVPDecoder>(codec_factory_, notify, source_,
                                           audio_render_.get());
  } else {
    auto notify =
        std::make_shared<Message>(kWhatVideoNotify, shared_from_this());
    video_render_ =
        std::make_shared<AVPVideoRender>(nullptr, sync_controller_.get());
    video_render_->SetSink(video_render_sink_);
    decoder = std::make_shared<AVPDecoder>(codec_factory_, notify, source_,
                                           video_render_.get());
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
    OnSeek(startUs, seekMode);
  }

  started_ = true;
  paused_ = true;

  sync_controller_ = std::make_shared<AVSyncControllerImpl>();

  PostScanSources();
}

void AvPlayer::OnStop() {
  if (!started_) {
    return;
  }
  started_ = false;
  paused_ = false;
  if (audio_decoder_) {
    audio_decoder_->Stop();
  }
  if (video_decoder_) {
    video_decoder_->Stop();
  }
  if (audio_render_) {
    audio_render_->Stop();
  }
  if (video_render_) {
    video_render_->Stop();
  }
  if (source_) {
    source_->Stop();
  }
}

void AvPlayer::OnPause() {
  if (paused_) {
    return;
  }
  paused_ = true;
  if (audio_decoder_) {
    audio_decoder_->Pause();
  }
  if (video_decoder_) {
    video_decoder_->Pause();
  }
  if (audio_render_) {
    audio_render_->Pause();
  }
  if (video_render_) {
    video_render_->Pause();
  }
}

void AvPlayer::OnResume() {
  if (!paused_) {
    return;
  }
  paused_ = false;
  if (audio_decoder_) {
    audio_decoder_->Resume();
  }
  if (video_decoder_) {
    video_decoder_->Resume();
  }
  if (audio_render_) {
    audio_render_->Resume();
  }
  if (video_render_) {
    video_render_->Resume();
  }
}

void AvPlayer::OnSeek(int64_t seek_to_us, SeekMode seek_mode) {
  if (source_) {
    source_->SeekTo(seek_to_us, seek_mode);
  }
  if (audio_decoder_) {
    audio_decoder_->Flush();
  }
  if (video_decoder_) {
    video_decoder_->Flush();
  }
  if (audio_render_) {
    audio_render_->Flush();
  }
  if (video_render_) {
    video_render_->Flush();
  }
}

void AvPlayer::PerformReset() {
  source_.reset();
}

/********************* ContentSource::Notify Start *********************/
void AvPlayer::OnPrepared(status_t err) {
  auto msg = std::make_shared<Message>(kWhatSourcePrepared, shared_from_this());
  msg->setInt32(kError, err);
  msg->post();
}

void AvPlayer::OnFlagsChanged(int32_t flags) {
  auto msg =
      std::make_shared<Message>(kWhatSourceFlagsChanged, shared_from_this());
  msg->setInt32(kFlags, flags);
  msg->post();
}

void AvPlayer::OnVideoSizeChanged(std::shared_ptr<MediaFormat>& format) {
  auto msg = std::make_shared<Message>(kWhatSourceVideoSizeChanged,
                                       shared_from_this());
  msg->setObject(kMediaFormat, std::static_pointer_cast<MessageObject>(format));
  msg->post();
}

void AvPlayer::OnSeekComplete() {
  auto msg =
      std::make_shared<Message>(kWhatSourceSeekComplete, shared_from_this());
  msg->post();
}

void AvPlayer::OnBufferingStart() {
  auto msg =
      std::make_shared<Message>(kWhatSourceBufferingStart, shared_from_this());
  msg->post();
}

void AvPlayer::OnBufferingUpdate(int percent) {
  auto msg =
      std::make_shared<Message>(kWhatSourceBufferingUpdate, shared_from_this());
  msg->setInt32(kPercent, percent);
  msg->post();
}

void AvPlayer::OnBufferingEnd() {
  auto msg =
      std::make_shared<Message>(kWhatSourceBufferingEnd, shared_from_this());
  msg->post();
}

void AvPlayer::OnCompletion() {
  auto msg =
      std::make_shared<Message>(kWhatSourceCompletion, shared_from_this());
  msg->post();
}

void AvPlayer::OnError(status_t error) {
  auto msg = std::make_shared<Message>(kWhatSourceError, shared_from_this());
  msg->setInt32(kError, error);
  msg->post();
}

void AvPlayer::OnFetchData(MediaType stream_type) {
  auto msg =
      std::make_shared<Message>(kWhatSourceFetchData, shared_from_this());
  msg->setInt32(kMediaType, static_cast<int32_t>(stream_type));
  msg->post();
}

/********************* ContentSource::Notify End *********************/

void AvPlayer::OnSourceNotify(const std::shared_ptr<Message>& msg) {
  int32_t what = 0;
  AVE_CHECK(msg->findInt32(kWhat, &what));
  switch (what) {
    case kWhatSourcePrepared: {
      AVE_LOG(LS_INFO) << "source prepared: " << source_;
      if (source_ == nullptr) {
        return;
      }
      status_t err = ave::OK;
      AVE_CHECK(msg->findInt32(kError, &err));
      if (err != ave::OK) {
      } else {
        prepared_ = true;
      }

      // TODO(youfa) new msg here
      // notifyListner(kWhatPrepared, msg);

      break;
    }
    default:
      break;
  }
}

void AvPlayer::OnDecoderNotify(const std::shared_ptr<Message>& msg) {
  int32_t what = 0;
  AVE_CHECK(msg->findInt32(kWhat, &what));
  switch (what) {
    case AVPDecoder::kWhatVideoSizeChanged: {
      std::shared_ptr<MessageObject> obj;
      msg->findObject(kMediaFormat, obj);
      auto format = std::dynamic_pointer_cast<MediaFormat>(obj);
      if (listener_.lock()) {
        // TODO: Implement onVideoSizeChanged in Listener interface
        // listener_.lock()->onVideoSizeChanged(format->width(),
        // format->height());
      }
      break;
    }
    case AVPDecoder::kWhatAudioOutputFormatChanged: {
      // TODO: notify listener
      break;
    }
    case AVPDecoder::kWhatEOS: {
      int32_t is_audio = 0;
      msg->findInt32("is_audio", &is_audio);
      if (is_audio) {
        audio_eos_ = true;
      } else {
        video_eos_ = true;
      }

      if (audio_eos_ && video_eos_) {
        if (listener_.lock()) {
          listener_.lock()->onCompletion();
        }
      }
      break;
    }
    case AVPDecoder::kWhatError: {
      status_t err = UNKNOWN_ERROR;
      msg->findInt32(kError, &err);
      if (listener_.lock()) {
        listener_.lock()->onError(err);
      }
      break;
    }
    default:
      break;
  }
}

void AvPlayer::OnRenderNotify(const std::shared_ptr<Message>& msg) {}

void AvPlayer::onMessageReceived(const std::shared_ptr<Message>& message) {
  AVE_LOG(LS_DEBUG) << "AvPlayer::onMessageReceived:" << message->what();
  switch (message->what()) {
      /************* from avplayer ***************/
    case kWhatSetDataSource: {
      std::shared_ptr<MessageObject> obj;
      message->findObject(kContentSource, obj);
      source_ = std::dynamic_pointer_cast<ContentSource>(obj);
      // notifyListner(kWhatSetDataSourceCompleted,
      // std::make_shared<Message>());
      break;
    }

      // case kWhatSetAudioSink: {
      //   std::shared_ptr<MessageObject> obj;
      //   message->findObject("audioSink", obj);
      //   mAudioSink = std::dynamic_pointer_cast<AudioSink>(obj);
      //   break;
      // }

    case kWhatSetVideoRender: {
      std::shared_ptr<MessageObject> obj;
      message->findObject(kVideoRender, obj);
      video_render_sink_ = std::dynamic_pointer_cast<VideoRender>(obj);
      break;
    }

    case kWhatPrepare: {
      source_->Prepare();
      break;
    }

    case kWhatStart: {
      OnStart();
      break;
    }

    case kWhatStop: {
      OnStop();
      break;
    }

    case kWhatScanSources: {
      AVE_LOG(LS_INFO) << "kWhatScanSources";
      scan_sources_pending_ = false;

      bool rescan = false;
      if (video_render_sink_ != nullptr && video_decoder_ == nullptr) {
        if (InstantiateDecoder(false, video_decoder_) == WOULD_BLOCK) {
          rescan = true;
        }
      }

      if (audio_device_ != nullptr && audio_decoder_ == nullptr) {
        if (InstantiateDecoder(true, audio_decoder_) == WOULD_BLOCK) {
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
      int64_t seek_to_us = 0;
      message->findInt64(kSeekToUs, &seek_to_us);
      int32_t seek_mode = 0;
      message->findInt32(kSeekMode, &seek_mode);
      OnSeek(seek_to_us, static_cast<SeekMode>(seek_mode));
      break;
    }

    case kWhatPause: {
      OnPause();
      break;
    }

    case kWhatResume: {
      OnResume();
      break;
    }

    case kWhatReset: {
      PerformReset();
    } break;

    case kWhatSourceNotify: {
      OnSourceNotify(message);
      break;
    }
    case kWhatAudioNotify:
    case kWhatVideoNotify: {
      OnDecoderNotify(message);
      break;
    }

    case kWhatRendererNotify: {
      OnRenderNotify(message);
      break;
    }
    default:
      break;
  }
}

}  // namespace player
}  // namespace ave
