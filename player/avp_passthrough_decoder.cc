/*
 * avp_passthrough_decoder.cc
 * Copyright (C) 2025 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "avp_passthrough_decoder.h"

#include <cstdio>
#include <cstdlib>

#include "base/logging.h"
#include "media/foundation/media_errors.h"
#include "media/foundation/media_packet.h"

#include "player/message_def.h"

using ave::media::MediaPacket;

namespace ave {
namespace player {

namespace {
// Maximum size of cached data before we stop fetching
const size_t kMaxCachedBytes = 200000;

// Optimal buffer size for power consumption
const size_t kAggregateBufferSizeBytes = 24 * 1024;
}  // namespace

AVPPassthroughDecoder::AVPPassthroughDecoder(
    const std::shared_ptr<Message>& notify,
    const std::shared_ptr<ContentSource>& source,
    const std::shared_ptr<AVPRender>& render)
    : AVPDecoderBase(notify, source, render),
      skip_rendering_until_media_time_us_(-1LL),
      reached_eos_(true),
      pending_buffers_to_drain_(0),
      cached_bytes_(0),
      pending_audio_access_unit_(nullptr),
      pending_audio_err_(OK),
      buffer_generation_(0) {
  AVE_LOG(LS_VERBOSE) << "AVPPassthroughDecoder created";
}

AVPPassthroughDecoder::~AVPPassthroughDecoder() {
  AVE_LOG(LS_VERBOSE) << "~AVPPassthroughDecoder";
}

void AVPPassthroughDecoder::OnConfigure(
    const std::shared_ptr<MediaFormat>& format) {
  AVE_LOG(LS_VERBOSE) << "OnConfigure";

  cached_bytes_ = 0;
  pending_buffers_to_drain_ = 0;
  reached_eos_ = false;
  ++buffer_generation_;

  // Request input buffers to start the decoding process
  OnRequestInputBuffers();
}

void AVPPassthroughDecoder::OnSetParameters(
    const std::shared_ptr<Message>& params) {
  AVE_LOG(LS_VERBOSE) << "OnSetParameters: " << params->what();
  // TODO: Implement parameter setting for passthrough decoder
}

void AVPPassthroughDecoder::OnSetVideoRender(
    const std::shared_ptr<VideoRender>& video_render) {
  // Passthrough decoder doesn't need video render
  AVE_LOG(LS_VERBOSE) << "OnSetVideoRender: ignored for passthrough decoder";
}

void AVPPassthroughDecoder::OnStart() {
  AVE_LOG(LS_VERBOSE) << "OnStart";
  OnRequestInputBuffers();
}

void AVPPassthroughDecoder::OnPause() {
  AVE_LOG(LS_VERBOSE) << "OnPause";
  // Passthrough decoder doesn't need special pause handling
}

void AVPPassthroughDecoder::OnResume() {
  AVE_LOG(LS_VERBOSE) << "OnResume";
  OnRequestInputBuffers();
}

void AVPPassthroughDecoder::OnFlush() {
  AVE_LOG(LS_VERBOSE) << "OnFlush";
  DoFlush(true /* notifyComplete */);
}

void AVPPassthroughDecoder::OnShutdown() {
  AVE_LOG(LS_VERBOSE) << "OnShutdown";
  ++buffer_generation_;
  skip_rendering_until_media_time_us_ = -1;
  reached_eos_ = true;

  if (avp_render_) {
    avp_render_->Flush();
  }
}

bool AVPPassthroughDecoder::IsStaleReply(const std::shared_ptr<Message>& msg) {
  int32_t generation{};
  AVE_CHECK(msg->findInt32("generation", &generation));
  return generation != buffer_generation_;
}

bool AVPPassthroughDecoder::IsDoneFetching() const {
  AVE_LOG(LS_VERBOSE) << "IsDoneFetching: cached_bytes=" << cached_bytes_
                      << ", reached_eos=" << reached_eos_
                      << ", paused=" << paused_;

  return cached_bytes_ >= kMaxCachedBytes || reached_eos_ || paused_;
}

bool AVPPassthroughDecoder::DoRequestInputBuffers() {
  status_t err = OK;
  while (!IsDoneFetching()) {
    std::shared_ptr<Message> msg = std::make_shared<Message>();

    err = FetchInputData(msg);

    if (err != OK) {
      break;
    }

    OnInputBufferFilled(msg);
  }

  return (err == WOULD_BLOCK) && (source_->FeedMoreESData() == OK);
}

std::shared_ptr<MediaPacket> AVPPassthroughDecoder::AggregateBuffer(
    const std::shared_ptr<MediaPacket>& packet) {
  std::shared_ptr<MediaPacket> aggregate;

  if (packet == nullptr) {
    aggregate = aggregate_buffer_;
    aggregate_buffer_ = nullptr;
    return aggregate;
  }

  auto small_size = packet->size();
  if (aggregate_buffer_ == nullptr) {
    if (small_size < (kAggregateBufferSizeBytes / 3)) {
      return nullptr;
    }
    aggregate_buffer_ = MediaPacket::CreateShared(kAggregateBufferSizeBytes);
  }

  if (aggregate_buffer_ != nullptr) {
    auto small_timestamp = packet->meta()->pts();
    auto big_timestamp = aggregate_buffer_->meta()->pts();

    auto big_size = aggregate_buffer_->size();
    auto room_left = aggregate_buffer_->size() - big_size;

    // Should we save this small buffer for the next big buffer?
    // If the first small buffer did not have a timestamp then save
    // any buffer that does have a timestamp until the next big buffer.
    if ((small_size > room_left) ||
        (big_timestamp.IsInfinite() && (big_size > 0) &&
         small_timestamp.IsFinite())) {
      pending_audio_err_ = OK;
      pending_audio_access_unit_ = packet;
      aggregate = aggregate_buffer_;
      aggregate_buffer_ = nullptr;
    } else {
      // Grab time from first small buffer if available.
      if ((big_size == 0) && small_timestamp.IsFinite()) {
        aggregate_buffer_->meta()->SetPts(small_timestamp);
      }
      // Append small buffer to the bigger buffer.
      memcpy(const_cast<uint8_t*>(aggregate_buffer_->data() + big_size),
             packet->data(), small_size);
      big_size += small_size;
      aggregate_buffer_->SetSize(big_size);

      AVE_LOG(LS_VERBOSE) << "feedDecoderInputData() smallSize = " << small_size
                          << ", bigSize = " << big_size
                          << ", capacity = " << aggregate_buffer_->size();
    }

  } else {
    aggregate = packet;
  }
  return aggregate;
}

status_t AVPPassthroughDecoder::DequeueAccessUnit(
    std::shared_ptr<MediaPacket>& packet) {
  status_t err = OK;
  if (pending_audio_access_unit_ != nullptr) {
    packet = pending_audio_access_unit_;
    pending_audio_access_unit_ = nullptr;
    err = pending_audio_err_;
    pending_audio_err_ = OK;
    AVE_LOG(LS_VERBOSE) << "feedDecoderInputData() use mPendingAudioAccessUnit";
  } else {
    err = source_->DequeueAccessUnit(MediaType::AUDIO, packet);
  }

  if (err == media::INFO_DISCONTINUITY || err == media::ERROR_END_OF_STREAM) {
    if (aggregate_buffer_ != nullptr) {
      pending_audio_err_ = err;
      pending_audio_access_unit_ = packet;
      packet.reset();
      AVE_LOG(LS_VERBOSE) << "return aggregated buffer and save err(=" << err
                          << ") for later";
      err = OK;
    }
  }
  return err;
}

status_t AVPPassthroughDecoder::FetchInputData(std::shared_ptr<Message>& msg) {
  std::shared_ptr<MediaPacket> packet;

  do {
    status_t err = DequeueAccessUnit(packet);

    if (err == WOULD_BLOCK) {
      packet = AggregateBuffer(nullptr);
      if (packet != nullptr) {
        break;
      }
      return err;
    }
    if (err != OK) {
      if (err == media::ERROR_END_OF_STREAM) {
        reached_eos_ = true;
        AVE_LOG(LS_INFO) << "Passthrough decoder: End of stream reached";
      } else {
        ReportError(err);
      }
      return err;
    }
    packet = AggregateBuffer(packet);
  } while (packet == nullptr);

  return OK;
}

void AVPPassthroughDecoder::OnInputBufferFilled(
    const std::shared_ptr<Message>& msg) {
  if (reached_eos_) {
    return;
  }

  // Create a virtual frame from the aggregated packet
  if (aggregate_buffer_ && aggregate_buffer_->size() > 0) {
    auto frame = std::make_shared<media::MediaFrame>(
        media::MediaFrame::Create(aggregate_buffer_->size()));
    frame->SetData(const_cast<uint8_t*>(aggregate_buffer_->data()),
                   aggregate_buffer_->size());
    // TODO: Set PTS when MediaFrame supports it

    // Send to renderer for AV sync
    if (avp_render_) {
      avp_render_->RenderFrame(frame);
    }

    cached_bytes_ += aggregate_buffer_->size();

    // Reset aggregate buffer
    // TODO: Fix MediaPacket interface
    // aggregate_buffer_->setRange(0, 0);
  }
}

void AVPPassthroughDecoder::OnBufferConsumed(int32_t size) {
  --pending_buffers_to_drain_;
  cached_bytes_ -= size;
  AVE_LOG(LS_VERBOSE) << "OnBufferConsumed: #ToDrain="
                      << pending_buffers_to_drain_
                      << ", cachedBytes=" << cached_bytes_;
  OnRequestInputBuffers();
}

void AVPPassthroughDecoder::DoFlush(bool notifyComplete) {
  ++buffer_generation_;
  skip_rendering_until_media_time_us_ = -1;
  if (aggregate_buffer_) {
    // TODO: Fix MediaPacket interface
    // aggregate_buffer_->setRange(0, 0);
  }

  if (avp_render_) {
    avp_render_->Flush();
  }

  pending_buffers_to_drain_ = 0;
  cached_bytes_ = 0;
  reached_eos_ = false;
}

void AVPPassthroughDecoder::onMessageReceived(
    const std::shared_ptr<Message>& msg) {
  switch (msg->what()) {
    case kWhatBufferConsumed: {
      if (!IsStaleReply(msg)) {
        int32_t size{};
        AVE_CHECK(msg->findInt32("size", &size));
        OnBufferConsumed(size);
      }
      break;
    }

    default:
      AVPDecoderBase::onMessageReceived(msg);
      break;
  }
}

}  // namespace player
}  // namespace ave
