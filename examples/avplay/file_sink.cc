/*
 * file_sink.cc
 * Copyright (C) 2025 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "file_sink.h"

#include <cstdint>
#include <cstring>

#include "base/logging.h"
#include "media/audio/channel_layout.h"
#include "media/audio/audio_format.h"

namespace ave {
namespace player {

// ─────────────────────────────────────────────────────────────────────────────
// WAV file header helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

constexpr uint32_t kWavAudioFormatPcm = 1;
constexpr uint32_t kWavAudioFormatFloat = 3;

struct WavHeader {
  // RIFF chunk
  char     riff[4]     = {'R', 'I', 'F', 'F'};
  uint32_t riff_size   = 36;  // updated at close
  char     wave[4]     = {'W', 'A', 'V', 'E'};
  // fmt sub-chunk
  char     fmt[4]      = {'f', 'm', 't', ' '};
  uint32_t fmt_size    = 16;
  uint16_t audio_fmt   = 1;   // PCM
  uint16_t channels    = 2;
  uint32_t sample_rate = 44100;
  uint32_t byte_rate   = 0;
  uint16_t block_align = 0;
  uint16_t bits        = 16;
  // data sub-chunk
  char     data[4]     = {'d', 'a', 't', 'a'};
  uint32_t data_size   = 0;   // updated at close
} __attribute__((packed));

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// YuvFileVideoRender
// ─────────────────────────────────────────────────────────────────────────────

YuvFileVideoRender::YuvFileVideoRender(const std::string& path)
    : path_(path) {
  file_ = fopen(path.c_str(), "wb");
  if (!file_) {
    AVE_LOG(LS_ERROR) << "YuvFileVideoRender: cannot open " << path;
  } else {
    AVE_LOG(LS_INFO) << "YuvFileVideoRender: writing to " << path;
  }
}

YuvFileVideoRender::~YuvFileVideoRender() {
  if (file_) {
    fclose(file_);
    file_ = nullptr;
    AVE_LOG(LS_INFO) << "YuvFileVideoRender: closed, frames=" << frames_written_
                     << " size=" << width_ << "x" << height_;
  }
}

void YuvFileVideoRender::OnFrame(
    const std::shared_ptr<media::MediaFrame>& frame) {
  if (!frame || !file_) {
    return;
  }

  int32_t w = frame->width();
  int32_t h = frame->height();
  int64_t pts_us = 0;

  auto* vinfo = frame->video_info();
  if (vinfo && vinfo->pts.IsFinite()) {
    pts_us = vinfo->pts.us();
  }

  if (w <= 0 || h <= 0) {
    AVE_LOG(LS_WARNING) << "YuvFileVideoRender: invalid frame size " << w << "x"
                        << h;
    return;
  }

  if (width_ == 0) {
    width_ = w;
    height_ = h;
    AVE_LOG(LS_INFO) << "YuvFileVideoRender: first frame " << w << "x" << h;
  }

  // Log A/V sync periodically
  int64_t fn = frames_written_.load();
  if (fn % 30 == 0) {
    int64_t pts_delta_us = pts_us - last_pts_us_;
    AVE_LOG(LS_INFO) << "Video frame #" << fn << " pts=" << pts_us / 1000
                     << "ms  delta=" << pts_delta_us << "us";
  }
  last_pts_us_ = pts_us;

  fwrite(frame->data(), 1, frame->size(), file_);
  frames_written_++;
}

// ─────────────────────────────────────────────────────────────────────────────
// WavFileAudioTrack
// ─────────────────────────────────────────────────────────────────────────────

WavFileAudioTrack::WavFileAudioTrack(const std::string& path) : path_(path) {}

WavFileAudioTrack::~WavFileAudioTrack() {
  Close();
}

status_t WavFileAudioTrack::Open(media::audio_config_t config,
                                 AudioCallback /*cb*/,
                                 void* /*cookie*/) {
  std::lock_guard<std::mutex> lock(mutex_);
  sample_rate_ = config.sample_rate;
  channels_ = media::ChannelLayoutToChannelCount(config.channel_layout);

  // Determine bits per sample from format
  switch (config.format) {
    case media::AUDIO_FORMAT_PCM_FLOAT:
      bytes_per_sample_ = 4;
      break;
    case media::AUDIO_FORMAT_PCM_8_BIT:
      bytes_per_sample_ = 1;
      break;
    case media::AUDIO_FORMAT_PCM_16_BIT:
    default:
      bytes_per_sample_ = 2;
      break;
  }

  file_ = fopen(path_.c_str(), "wb");
  if (!file_) {
    AVE_LOG(LS_ERROR) << "WavFileAudioTrack: cannot open " << path_;
    return -1;
  }

  AVE_LOG(LS_INFO) << "WavFileAudioTrack: writing to " << path_
                   << " sr=" << sample_rate_ << " ch=" << channels_
                   << " bps=" << bytes_per_sample_ * 8;

  WriteWavHeader();
  ready_ = true;
  return 0;
}

void WavFileAudioTrack::WriteWavHeader() {
  WavHeader hdr;
  hdr.channels    = static_cast<uint16_t>(channels_);
  hdr.sample_rate = sample_rate_;
  hdr.bits        = static_cast<uint16_t>(bytes_per_sample_ * 8);
  hdr.block_align = static_cast<uint16_t>(channels_ * bytes_per_sample_);
  hdr.byte_rate   = sample_rate_ * hdr.block_align;
  hdr.audio_fmt   = (bytes_per_sample_ == 4) ? kWavAudioFormatFloat
                                              : kWavAudioFormatPcm;
  fwrite(&hdr, sizeof(hdr), 1, file_);
}

void WavFileAudioTrack::UpdateWavHeader() {
  if (!file_) {
    return;
  }
  uint32_t data_size   = static_cast<uint32_t>(bytes_written_);
  uint32_t riff_size   = data_size + sizeof(WavHeader) - 8;
  // Patch riff_size at offset 4
  fseek(file_, 4, SEEK_SET);
  fwrite(&riff_size, 4, 1, file_);
  // Patch data_size at offset sizeof(WavHeader)-4
  fseek(file_, static_cast<long>(sizeof(WavHeader)) - 4, SEEK_SET);
  fwrite(&data_size, 4, 1, file_);
  fseek(file_, 0, SEEK_END);
}

ssize_t WavFileAudioTrack::Write(const void* buffer, size_t size,
                                 bool /*blocking*/) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!file_ || !ready_) {
    return -1;
  }
  size_t written = fwrite(buffer, 1, size, file_);
  bytes_written_ += written;
  frames_written_ +=
      static_cast<uint32_t>(written / (channels_ * bytes_per_sample_));

  static uint64_t log_counter = 0;
  if (log_counter++ % 100 == 0) {
    AVE_LOG(LS_INFO) << "WavFileAudioTrack: wrote " << bytes_written_
                     << " bytes total, ~" << bytes_written_ / (sample_rate_ * channels_ * bytes_per_sample_)
                     << "s";
  }
  return static_cast<ssize_t>(written);
}

status_t WavFileAudioTrack::Start() {
  playing_ = true;
  return 0;
}

void WavFileAudioTrack::Stop() {
  playing_ = false;
}

void WavFileAudioTrack::Flush() {}

void WavFileAudioTrack::Pause() {
  playing_ = false;
}

void WavFileAudioTrack::Close() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (file_) {
    UpdateWavHeader();
    fclose(file_);
    file_ = nullptr;
    ready_ = false;
    AVE_LOG(LS_INFO) << "WavFileAudioTrack: closed, bytes=" << bytes_written_
                     << " frames=" << frames_written_;
  }
}

status_t WavFileAudioTrack::GetPosition(uint32_t* position) const {
  if (position) {
    *position = frames_written_;
  }
  return 0;
}

int64_t WavFileAudioTrack::GetPlayedOutDurationUs(int64_t /*nowUs*/) const {
  if (sample_rate_ == 0) {
    return 0;
  }
  return static_cast<int64_t>(frames_written_) * 1000000LL / sample_rate_;
}

status_t WavFileAudioTrack::GetFramesWritten(uint32_t* frameswritten) const {
  if (frameswritten) {
    *frameswritten = frames_written_;
  }
  return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// FileAudioDevice
// ─────────────────────────────────────────────────────────────────────────────

FileAudioDevice::FileAudioDevice(const std::string& path) : path_(path) {}

std::shared_ptr<media::AudioTrack> FileAudioDevice::CreateAudioTrack() {
  return std::make_shared<WavFileAudioTrack>(path_);
}

}  // namespace player
}  // namespace ave
