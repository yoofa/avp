/*
 * ffmpeg_demuxer.cc
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "ffmpeg_demuxer.h"

#include <iostream>

#include "base/checks.h"
#include "base/hexdump.h"
#include "base/logging.h"

#include "media/foundation/media_meta.h"
#include "media/foundation/media_source.h"
#include "media/modules/ffmpeg/ffmpeg_utils.h"

namespace ave {
namespace player {

using ave::media::Message;

namespace {

void AppendTrackInfoToPacket(std::shared_ptr<MediaFrame>& packet,
                             std::shared_ptr<MediaMeta>& track_meta) {
  // Save PTS/DTS/duration before changing stream type (SetStreamType resets
  // info)
  auto saved_pts = packet->pts();
  auto saved_dts = packet->dts();
  auto saved_duration = packet->duration();

  // Set the correct stream type from the track metadata
  packet->SetStreamType(track_meta->stream_type());

  // Re-apply timestamps after stream type change
  if (!saved_pts.IsMinusInfinity()) {
    packet->SetPts(saved_pts);
  }
  if (!saved_dts.IsMinusInfinity()) {
    packet->SetDts(saved_dts);
  }
  if (!saved_duration.IsMinusInfinity()) {
    packet->SetDuration(saved_duration);
  }

  switch (track_meta->stream_type()) {
    case media::MediaType::AUDIO: {
      packet->SetChannelLayout(track_meta->channel_layout());
      packet->SetSampleRate(track_meta->sample_rate());
      packet->SetBitsPerSample(track_meta->bits_per_sample());
      packet->SetSamplesPerChannel(track_meta->samples_per_channel());
      packet->SetCodec(track_meta->codec());
      break;
    }
    case media::MediaType::VIDEO: {
      break;
    }

    default: {
      break;
    }
  }
}

}  // namespace

enum { kBufferSize = 32 * 1024 };
int64_t lastVideoTimeUs = 0;

static int AVIOReadOperation(void* opaque, uint8_t* buf, int size) {
  auto* data_source = reinterpret_cast<DataSource*>(opaque);
  ssize_t result = data_source->Read(buf, size);
  if (result == 0) {
    return AVERROR_EOF;
  }
  if (result < 0) {
    return AVERROR(EIO);
  }
  return static_cast<int>(result);
}

static int64_t AVIOSeekOperation(void* opaque, int64_t offset, int whence) {
  auto* data_source = reinterpret_cast<DataSource*>(opaque);
  int64_t new_offset = AVERROR(EIO);
  switch (whence) {
    case SEEK_SET:
      new_offset = data_source->Seek(offset, SEEK_SET);
      break;

    case SEEK_CUR: {
      // Translate to absolute position
      off64_t cur = 0;
      data_source->GetPosition(&cur);
      new_offset = data_source->Seek(cur + offset, SEEK_SET);
      break;
    }

    case SEEK_END: {
      // Translate to absolute position from end
      off64_t size = 0;
      data_source->GetSize(&size);
      new_offset = data_source->Seek(size + offset, SEEK_SET);
      break;
    }

    case AVSEEK_SIZE:
      data_source->GetSize(&new_offset);
      break;

    default:
      break;
  }
  if (new_offset < 0) {
    new_offset = AVERROR(EIO);
  }
  return new_offset;
}

////////////////////////////////////

using ave::media::Buffer;
using ave::media::MediaMeta;
using ave::media::MediaSource;

struct FFmpegSource : public MediaSource {
  FFmpegSource(FFmpegDemuxer* demuxer,
               size_t index,
               std::shared_ptr<MediaMeta> meta);
  ~FFmpegSource() override;

  status_t Start(std::shared_ptr<Message> params) override;
  status_t Stop() override;
  status_t Read(std::shared_ptr<MediaFrame>& packet,
                const ReadOptions* options) override;
  std::shared_ptr<MediaMeta> GetFormat() override;

 private:
  FFmpegDemuxer* demuxer_;
  size_t track_index;
  std::shared_ptr<MediaMeta> meta;
};

FFmpegSource::FFmpegSource(FFmpegDemuxer* demuxer,
                           size_t index,
                           std::shared_ptr<MediaMeta> meta)
    : demuxer_(demuxer), track_index(index), meta(std::move(meta)) {}

FFmpegSource::~FFmpegSource() = default;

status_t FFmpegSource::Start(std::shared_ptr<Message> params) {
  (void)params;  // Unused parameter
  return ave::OK;
}

status_t FFmpegSource::Stop() {
  return ave::OK;
}

status_t FFmpegSource::Read(std::shared_ptr<MediaFrame>& packet,
                            const ReadOptions* options) {
  auto ret = demuxer_->ReadAvFrame(packet, track_index, options);
  if (ret != ave::OK) {
    return ret;
  }
  if (!packet) {
    return ave::WOULD_BLOCK;
  }
  return ave::OK;
}

std::shared_ptr<MediaMeta> FFmpegSource::GetFormat() {
  return meta;
}

////////////////////////////////////
FFmpegDemuxer::TrackInfo::TrackInfo(size_t index,
                                    std::shared_ptr<MediaMeta> meta,
                                    std::shared_ptr<FFmpegSource> source)
    : track_index(index), meta(std::move(meta)), source(std::move(source)) {}

FFmpegDemuxer::TrackInfo::~TrackInfo() {
  if (bsf_ctx) {
    av_bsf_free(&bsf_ctx);
  }
}

size_t FFmpegDemuxer::TrackInfo::PacketSize() {
  return packets.size();
}

status_t FFmpegDemuxer::TrackInfo::EnqueuePacket(
    std::shared_ptr<MediaFrame> packet) {
  packets.push_back(std::move(packet));
  return ave::OK;
}

status_t FFmpegDemuxer::TrackInfo::DequeuePacket(
    std::shared_ptr<MediaFrame>& packet) {
  if (packets.empty()) {
    return ave::WOULD_BLOCK;
  }
  packet = packets.front();
  packets.pop_front();
  return ave::OK;
}

////////////////////////////////////

FFmpegDemuxer::FFmpegDemuxer(std::shared_ptr<ave::DataSource> data_source)
    : Demuxer(data_source) {
  av_log_set_level(AV_LOG_QUIET);
  av_format_context_ = avformat_alloc_context();
  av_io_context_ = avio_alloc_context(
      static_cast<unsigned char*>(av_malloc(kBufferSize)), kBufferSize, 0,
      data_source_.get(), &AVIOReadOperation, nullptr, &AVIOSeekOperation);
  av_io_context_->seekable = (data_source_->Flags() & DataSource::kSeekable)
                                 ? AVIO_SEEKABLE_NORMAL
                                 : 0;
  av_io_context_->write_flag = 0;

  av_format_context_->flags |= AVFMT_FLAG_CUSTOM_IO;
  av_format_context_->pb = av_io_context_;
}

FFmpegDemuxer::~FFmpegDemuxer() = default;

status_t FFmpegDemuxer::Init() {
  AVE_LOG(LS_INFO) << "Init FFmpegDemuxer";
  AVIOSeekOperation(av_io_context_->opaque, 0, SEEK_CUR);
  int ret = OK;
  ret = avformat_open_input(&av_format_context_, nullptr, nullptr, nullptr);
  AVE_LOG(LS_VERBOSE) << "avformat_open_input ret=" << ret;
  if (ret < 0 || av_format_context_ == nullptr) {
    return ret < 0 ? ret : media::ERROR_MALFORMED;
  }

  // Limit probing so avformat_find_stream_info doesn't try to decode frames
  // to detect stream parameters.  Container formats like MP4/MKV embed all
  // codec parameters in their headers, so 1 MB / 1 s is more than enough.
  av_format_context_->probesize = 1 * 1024 * 1024;          // 1 MB
  av_format_context_->max_analyze_duration = AV_TIME_BASE;  // 1 s

  ret = avformat_find_stream_info(av_format_context_, nullptr);
  AVE_LOG(LS_VERBOSE) << "avformat_find_stream_info ret=" << ret;
  source_format_ = MediaMeta::CreatePtr(ave::media::MediaType::AUDIO,
                                        MediaMeta::FormatType::kTrack);

  AVE_LOG(LS_VERBOSE) << "init, streams: " << av_format_context_->nb_streams;

  source_format_->SetDuration(
      base::TimeDelta::Micros(av_format_context_->duration));
  source_format_->SetBitrate(av_format_context_->bit_rate);

  for (size_t i = 0; i < av_format_context_->nb_streams; i++) {
    const AVStream* avStream = av_format_context_->streams[i];
    if (avStream != nullptr) {
      AddTrack(avStream, i);
    }
  }

  return ret;
}

status_t FFmpegDemuxer::AddTrack(const AVStream* avStream, size_t index) {
  std::shared_ptr<MediaMeta> meta =
      media::ffmpeg_utils::ExtractMetaFromAVStream(avStream);
  // if (avStream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
  // AVE_LOG(LS_INFO) << "dump(" << avStream->codecpar->codec_type << "):";
  // hexdump(avStream->codecpar->extradata, avStream->codecpar->extradata_size);
  //}

  std::shared_ptr<FFmpegSource> source(
      std::make_shared<FFmpegSource>(this, index, meta));
  tracks_.emplace_back(index, meta, source);

  // Set up AVCC→Annex-B bitstream filter for H.264/HEVC video tracks
  auto& track = tracks_.back();
  const char* bsf_name = nullptr;
  if (avStream->codecpar->codec_id == AV_CODEC_ID_H264) {
    bsf_name = "h264_mp4toannexb";
  } else if (avStream->codecpar->codec_id == AV_CODEC_ID_HEVC) {
    bsf_name = "hevc_mp4toannexb";
  }
  if (bsf_name) {
    const AVBitStreamFilter* bsf = av_bsf_get_by_name(bsf_name);
    if (bsf && av_bsf_alloc(bsf, &track.bsf_ctx) == 0) {
      avcodec_parameters_copy(track.bsf_ctx->par_in, avStream->codecpar);
      track.bsf_ctx->time_base_in = avStream->time_base;
      if (av_bsf_init(track.bsf_ctx) < 0) {
        av_bsf_free(&track.bsf_ctx);
        track.bsf_ctx = nullptr;
      }
    }
  }

  return ave::OK;
}

size_t FFmpegDemuxer::GetTrackCount() {
  return tracks_.size();
}

std::shared_ptr<MediaSource> FFmpegDemuxer::GetTrack(size_t trackIndex) {
  if (trackIndex >= tracks_.size()) {
    return nullptr;
  }
  return tracks_[trackIndex].source;
}

status_t FFmpegDemuxer::GetFormat(std::shared_ptr<MediaMeta>& format) {
  format = source_format_;
  return ave::OK;
}

status_t FFmpegDemuxer::GetTrackFormat(std::shared_ptr<MediaMeta>& format,
                                       size_t trackIndex) {
  if (trackIndex >= tracks_.size()) {
    return ave::UNKNOWN_ERROR;
  }
  format = tracks_[trackIndex].meta;
  return ave::OK;
}

status_t FFmpegDemuxer::ReadAnAvPacket(size_t index) {
  AVPacket pkt;
  status_t err = OK;

  while (true) {
    err = av_read_frame(av_format_context_, &pkt);
    if (err < 0) {
      return media::ERROR_END_OF_STREAM;
    }

    AVE_DCHECK_GE(pkt.stream_index, 0);
    AVE_DCHECK_LT(static_cast<size_t>(pkt.stream_index), tracks_.size());

    if (pkt.stream_index == 0) {
      // AVE_LOG(LS_INFO)
      //     << "readAnAvPacket, index:" << pkt.stream_index << ", time_base:"
      //     << av_format_context_->streams[pkt.stream_index]->time_base.num <<
      //     "/"
      //     << av_format_context_->streams[pkt.stream_index]->time_base.den
      //     << ", pts:" << pkt.pts << ",time_us:"
      //     << media::ffmpeg_utils::ConvertFromTimeBase(
      //            av_format_context_->streams[pkt.stream_index]->time_base,
      //            pkt.pts)
      //     << ", diff:" << (pkt.pts - lastVideoTimeUs);

      lastVideoTimeUs = pkt.pts;
    }

    if (pkt.stream_index >= 0 &&
        pkt.stream_index < static_cast<int>(tracks_.size())) {
      auto& track = tracks_[pkt.stream_index];
      // Apply bitstream filter (e.g. AVCC→Annex-B for H.264/HEVC)
      if (track.bsf_ctx) {
        AVPacket* filtered_pkt = av_packet_alloc();
        av_bsf_send_packet(track.bsf_ctx, &pkt);
        if (av_bsf_receive_packet(track.bsf_ctx, filtered_pkt) == 0) {
          // av_bsf_receive_packet may not set time_base; use bsf output
          // time_base
          if (filtered_pkt->time_base.den == 0 ||
              filtered_pkt->time_base.num == 0) {
            filtered_pkt->time_base = track.bsf_ctx->time_base_out;
          }
          auto packet =
              media::ffmpeg_utils::CreateMediaFrameFromAVPacket(filtered_pkt);
          AppendTrackInfoToPacket(packet, track.meta);
          track.EnqueuePacket(packet);
        }
        av_packet_free(&filtered_pkt);
      } else {
        // av_read_frame may not set pkt.time_base; use stream time_base
        if (pkt.time_base.den == 0 || pkt.time_base.num == 0) {
          pkt.time_base =
              av_format_context_->streams[pkt.stream_index]->time_base;
        }
        auto packet = media::ffmpeg_utils::CreateMediaFrameFromAVPacket(&pkt);
        // append track info to packet
        AppendTrackInfoToPacket(packet, track.meta);
        track.EnqueuePacket(packet);
      }
    }
    if (static_cast<size_t>(pkt.stream_index) == index) {
      break;
    }
  }

  return err;
}

status_t FFmpegDemuxer::ReadAvFrame(std::shared_ptr<MediaFrame>& packet,
                                    size_t index,
                                    const MediaSource::ReadOptions* options) {
  if (index >= tracks_.size()) {
    return ave::UNKNOWN_ERROR;
  }

  // TODO(youfa) readOption

  if (tracks_[index].PacketSize() == 0) {
    auto st = ReadAnAvPacket(index);
    if (st != ave::OK) {
      return st;
    }
  }

  auto st = tracks_[index].DequeuePacket(packet);
  if (st != ave::OK) {
    return st;
  }
  return ave::OK;
}

const char* FFmpegDemuxer::name() {
  return "FFmpeg-Demuxer";
}

}  // namespace player
}  // namespace ave
