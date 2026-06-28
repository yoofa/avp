/*
 * mkv_demuxer.cc
 * Copyright (C) 2026 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "mkv_demuxer.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "base/ave_config.h"
#include "base/logging.h"
#include "media/audio/channel_layout.h"
#include "media/codec/codec_id.h"
#include "media/foundation/color_space.h"
#include "media/foundation/media_mimes.h"

namespace {

bool DetectDolbyVision(const uint8_t* data,
                       size_t size,
                       int32_t& profile,
                       int32_t& level) {
  if (size < 8) {
    return false;
  }
  for (size_t i = 0; i <= size - 8; ++i) {
    if (data[i] == 'd' && data[i + 1] == 'v' &&
        (data[i + 2] == 'c' || data[i + 2] == 'C') && data[i + 3] == 'C') {
      if (i >= 4) {
        uint32_t box_size = (static_cast<uint32_t>(data[i - 4]) << 24) |
                            (static_cast<uint32_t>(data[i - 3]) << 16) |
                            (static_cast<uint32_t>(data[i - 2]) << 8) |
                            data[i - 1];
        if (box_size >= 8 && i - 4 + box_size <= size) {
          const uint8_t* dvcc_data = &data[i + 4];
          profile = (dvcc_data[2] >> 1) & 0x7f;
          level = ((dvcc_data[2] & 0x01) << 5) | ((dvcc_data[3] >> 3) & 0x1f);
          return true;
        }
      }
      if (i + 4 + 4 <= size) {
        const uint8_t* dvcc_data = &data[i + 4];
        profile = (dvcc_data[2] >> 1) & 0x7f;
        level = ((dvcc_data[2] & 0x01) << 5) | ((dvcc_data[3] >> 3) & 0x1f);
        return true;
      }
    }
  }
  return false;
}

}  // namespace

#include <ebml/EbmlBinary.h>
#include <ebml/EbmlFloat.h>
#include <ebml/EbmlHead.h>
#include <ebml/EbmlMaster.h>
#include <ebml/EbmlSInteger.h>
#include <ebml/EbmlStream.h>
#include <ebml/EbmlString.h>
#include <ebml/EbmlUInteger.h>
#include <ebml/EbmlUnicodeString.h>
#include <ebml/IOCallback.h>

#include <matroska/KaxBlock.h>
#include <matroska/KaxCluster.h>
#include <matroska/KaxCues.h>
#include <matroska/KaxCuesData.h>
#include <matroska/KaxSegment.h>
#include <matroska/KaxSemantic.h>
#include <matroska/KaxTracks.h>

using namespace libebml;
using namespace libmatroska;

namespace ave {
namespace player {

class DataSourceIOCallback : public libebml::IOCallback {
 public:
  explicit DataSourceIOCallback(std::shared_ptr<DataSource> source)
      : source_(std::move(source)) {
    mb_eof = false;
  }
  ~DataSourceIOCallback() override = default;

  std::size_t read(void* buffer, std::size_t size) override {
    if (size == 0 || mb_eof) {
      return 0;
    }
    ssize_t ret = source_->Read(buffer, size);
    if (ret <= 0) {
      mb_eof = true;
      return 0;
    }
    return static_cast<std::size_t>(ret);
  }

  void setFilePointer(int64_t offset,
                      seek_mode mode = seek_beginning) override {
    off64_t pos = 0;
    switch (mode) {
      case seek_beginning:
        pos = offset;
        break;
      case seek_end: {
        off64_t total_size = 0;
        source_->GetSize(&total_size);
        pos = total_size - offset;
        break;
      }
      case seek_current: {
        off64_t current_pos = 0;
        source_->GetPosition(&current_pos);
        pos = current_pos + offset;
        break;
      }
    }

    ssize_t ret = source_->Seek(pos, SEEK_SET);
    mb_eof = ret < 0;
  }

  std::size_t write(const void* /* buffer */, std::size_t /* size */) override {
    return 0;
  }

  std::uint64_t getFilePointer() override {
    off64_t pos = 0;
    source_->GetPosition(&pos);
    return static_cast<std::uint64_t>(pos);
  }

  void close() override {}

 private:
  std::shared_ptr<DataSource> source_;
  bool mb_eof;
};

// ========== MkvSource ==========

MkvSource::MkvSource(MkvDemuxer* demuxer,
                     size_t track_index,
                     std::shared_ptr<MediaMeta> format)
    : demuxer_(demuxer),
      track_index_(track_index),
      format_(std::move(format)) {}

status_t MkvSource::Start(std::shared_ptr<ave::media::Message> /*params*/) {
  started_ = true;
  return OK;
}

status_t MkvSource::Stop() {
  started_ = false;
  return OK;
}

std::shared_ptr<MediaMeta> MkvSource::GetFormat() {
  return format_;
}

status_t MkvSource::Read(std::shared_ptr<MediaFrame>& frame,
                         const ReadOptions* options) {
  if (!started_) {
    return NO_INIT;
  }
  return demuxer_->ReadFrame(track_index_, frame, options);
}

// ========== MkvDemuxer ==========

MkvDemuxer::MkvDemuxer(std::shared_ptr<DataSource> data_source)
    : Demuxer(std::move(data_source)),
      en_debug_log_(base::AveConfig::GetInstance().demuxer_logs_enabled()) {
  AVE_LOG(LS_INFO) << "MkvDemuxer created";
}

MkvDemuxer::~MkvDemuxer() {
  delete current_element_;
  delete segment_;
  AVE_LOG(LS_INFO) << "MkvDemuxer destroyed";
}

std::optional<DemuxerSniffResult> MkvDemuxer::Sniff(
    const std::shared_ptr<DataSource>& data_source) {
  constexpr size_t kProbeSize = 4096;
  std::array<uint8_t, kProbeSize> probe = {};
  if (!data_source || data_source->ReadAt(0, probe.data(), probe.size()) < 4) {
    return std::nullopt;
  }
  if (probe[0] != 0x1a || probe[1] != 0x45 || probe[2] != 0xdf ||
      probe[3] != 0xa3) {
    return std::nullopt;
  }

  const std::string_view header(reinterpret_cast<const char*>(probe.data()),
                                probe.size());
  if (header.find("webm") != std::string_view::npos) {
    return DemuxerSniffResult{"video/webm"};
  }
  if (header.find("matroska") != std::string_view::npos) {
    return DemuxerSniffResult{media::MEDIA_MIMETYPE_CONTAINER_MATROSKA};
  }
  return std::nullopt;
}

std::vector<std::string> MkvDemuxer::SupportedContainerMimeTypes() {
  return {media::MEDIA_MIMETYPE_CONTAINER_MATROSKA, "audio/x-matroska",
          "video/webm", "audio/webm"};
}

const char* MkvDemuxer::name() {
  return "MkvDemuxer";
}

status_t MkvDemuxer::Init() {
  // libebml reads sequentially through DataSourceIOCallback. Previous
  // demuxer probes use ReadAt(), which may leave the shared DataSource at a
  // non-zero position, so reset it before looking for the EBML header.
  if (data_source_->Seek(0, SEEK_SET) < 0) {
    AVE_LOG(LS_ERROR) << "MkvDemuxer::Init: Failed to seek to file start";
    return media::ERROR_IO;
  }

  io_callback_ = std::make_unique<DataSourceIOCallback>(data_source_);
  stream_ = std::make_unique<libebml::EbmlStream>(*io_callback_);

  try {
    // 1. Find and parse EBML Head
    EbmlElement* head = stream_->FindNextID(EBML_INFO(EbmlHead), UINT64_MAX);
    if (!head) {
      AVE_LOG(LS_ERROR) << "MkvDemuxer::Init: Failed to find EbmlHead";
      return media::ERROR_MALFORMED;
    }
    head->ReadData(stream_->I_O(), SCOPE_ALL_DATA);

    auto* doc_type_el = static_cast<EDocType*>(
        static_cast<EbmlMaster*>(head)->FindFirstElt(EBML_INFO(EDocType)));
    if (!doc_type_el) {
      delete head;
      AVE_LOG(LS_ERROR) << "MkvDemuxer::Init: Failed to find EDocType";
      return media::ERROR_MALFORMED;
    }
    std::string doctype = doc_type_el->GetValue();
    if (doctype != "matroska" && doctype != "webm") {
      delete head;
      AVE_LOG(LS_ERROR) << "MkvDemuxer::Init: Unsupported doctype: " << doctype;
      return media::ERROR_UNSUPPORTED;
    }
    delete head;

    // 2. Find KaxSegment
    segment_ = static_cast<KaxSegment*>(
        stream_->FindNextID(EBML_INFO(KaxSegment), UINT64_MAX));
    if (!segment_) {
      AVE_LOG(LS_ERROR) << "MkvDemuxer::Init: Failed to find KaxSegment";
      return media::ERROR_MALFORMED;
    }

    // 3. Parse KaxSegment children
    int upper = 0;
    EbmlElement* child = stream_->FindNextElement(
        EBML_CLASS_CONTEXT(KaxSegment), upper, UINT64_MAX, true);
    while (child != nullptr && upper <= 0) {
      if (EbmlId(*child) == EBML_ID(KaxInfo)) {
        auto* info = static_cast<KaxInfo*>(child);
        info->ReadData(stream_->I_O(), SCOPE_ALL_DATA);

        auto* tc_scale_el = static_cast<KaxTimestampScale*>(
            info->FindFirstElt(EBML_INFO(KaxTimestampScale)));
        if (tc_scale_el) {
          timecode_scale_ = tc_scale_el->GetValue();
        }

        auto* duration_el = static_cast<KaxDuration*>(
            info->FindFirstElt(EBML_INFO(KaxDuration)));
        if (duration_el) {
          duration_us_ = static_cast<int64_t>(duration_el->GetValue() *
                                              timecode_scale_ / 1000);
        }
      } else if (EbmlId(*child) == EBML_ID(KaxTracks)) {
        auto* tracks = static_cast<KaxTracks*>(child);
        tracks->ReadData(stream_->I_O(), SCOPE_ALL_DATA);

        const std::vector<EbmlElement*>& children = tracks->GetElementList();
        for (EbmlElement* track_el : children) {
          if (EbmlId(*track_el) == EBML_ID(KaxTrackEntry)) {
            auto* entry = static_cast<KaxTrackEntry*>(track_el);

            auto* num_el = static_cast<KaxTrackNumber*>(
                entry->FindFirstElt(EBML_INFO(KaxTrackNumber)));
            auto* type_el = static_cast<KaxTrackType*>(
                entry->FindFirstElt(EBML_INFO(KaxTrackType)));
            auto* codec_el = static_cast<KaxCodecID*>(
                entry->FindFirstElt(EBML_INFO(KaxCodecID)));

            if (!num_el || !type_el || !codec_el) {
              continue;
            }

            uint32_t track_num = num_el->GetValue();
            uint8_t track_type = type_el->GetValue();
            std::string codec_str = codec_el->GetValue();

            MediaType media_type = MediaType::UNKNOWN;
            if (track_type == 1) {
              media_type = MediaType::VIDEO;
            } else if (track_type == 2) {
              media_type = MediaType::AUDIO;
            } else {
              continue;
            }

            // Map Codec
            media::CodecId codec_id = media::CodecId::AVE_CODEC_ID_NONE;
            std::string mime;
            if (codec_str == "V_MPEG4/ISO/AVC") {
              codec_id = media::CodecId::AVE_CODEC_ID_H264;
              mime = media::MEDIA_MIMETYPE_VIDEO_AVC;
            } else if (codec_str == "V_MPEGH/ISO/HEVC") {
              codec_id = media::CodecId::AVE_CODEC_ID_HEVC;
              mime = media::MEDIA_MIMETYPE_VIDEO_HEVC;
            } else if (codec_str == "V_VP8") {
              codec_id = media::CodecId::AVE_CODEC_ID_VP8;
              mime = media::MEDIA_MIMETYPE_VIDEO_VP8;
            } else if (codec_str == "V_VP9") {
              codec_id = media::CodecId::AVE_CODEC_ID_VP9;
              mime = media::MEDIA_MIMETYPE_VIDEO_VP9;
            } else if (codec_str == "V_AV1") {
              codec_id = media::CodecId::AVE_CODEC_ID_AV1;
              mime = media::MEDIA_MIMETYPE_VIDEO_AV1;
            } else if (codec_str == "A_AAC" || codec_str == "A_AAC/MPEG4/LC") {
              codec_id = media::CodecId::AVE_CODEC_ID_AAC;
              mime = "audio/mp4a-latm";
            } else if (codec_str == "A_MPEG/L3") {
              codec_id = media::CodecId::AVE_CODEC_ID_MP3;
              mime = "audio/mpeg";
            } else if (codec_str == "A_VORBIS") {
              codec_id = media::CodecId::AVE_CODEC_ID_VORBIS;
              mime = "audio/x-vorbis";
            } else if (codec_str == "A_OPUS") {
              codec_id = media::CodecId::AVE_CODEC_ID_OPUS;
              mime = "audio/opus";
            } else if (codec_str == "A_FLAC") {
              codec_id = media::CodecId::AVE_CODEC_ID_FLAC;
              mime = "audio/flac";
            }

            auto meta =
                MediaMeta::CreatePtr(media_type, MediaMeta::FormatType::kTrack);
            meta->SetStreamType(media_type);
            meta->SetCodec(codec_id);

            // Codec private data
            auto* private_el = static_cast<KaxCodecPrivate*>(
                entry->FindFirstElt(EBML_INFO(KaxCodecPrivate)));
            int32_t dv_profile = 0;
            int32_t dv_level = 0;
            bool is_dolby_vision = false;
            if (private_el && private_el->GetSize() > 0) {
              meta->SetPrivateData(private_el->GetSize(),
                                   private_el->GetBuffer());
              if (media_type == MediaType::VIDEO) {
                is_dolby_vision = DetectDolbyVision(
                    static_cast<const uint8_t*>(private_el->GetBuffer()),
                    private_el->GetSize(), dv_profile, dv_level);
              }
            }

            if (is_dolby_vision) {
              mime = media::MEDIA_MIMETYPE_VIDEO_DOLBY_VISION;
              meta->SetMime(mime.c_str());
              meta->SetCodecProfile(dv_profile);
              meta->SetCodecLevel(dv_level);
              AVE_LOG(LS_INFO)
                  << "MkvDemuxer::Init: Detected Dolby Vision profile="
                  << dv_profile << ", level=" << dv_level;
            } else if (!mime.empty()) {
              meta->SetMime(mime.c_str());
            }

            // Video parameters
            if (media_type == MediaType::VIDEO) {
              auto* video = static_cast<KaxTrackVideo*>(
                  entry->FindFirstElt(EBML_INFO(KaxTrackVideo)));
              if (video) {
                auto* width_el = static_cast<KaxVideoPixelWidth*>(
                    video->FindFirstElt(EBML_INFO(KaxVideoPixelWidth)));
                auto* height_el = static_cast<KaxVideoPixelHeight*>(
                    video->FindFirstElt(EBML_INFO(KaxVideoPixelHeight)));
                if (width_el) {
                  meta->SetWidth(width_el->GetValue());
                }
                if (height_el) {
                  meta->SetHeight(height_el->GetValue());
                }

                auto* colour = static_cast<KaxVideoColour*>(
                    video->FindFirstElt(EBML_INFO(KaxVideoColour)));
                if (colour) {
                  media::ColorSpace color_space;

                  auto* matrix_el = static_cast<KaxVideoColourMatrix*>(
                      colour->FindFirstElt(EBML_INFO(KaxVideoColourMatrix)));
                  if (matrix_el) {
                    color_space.set_matrix_from_uint8(
                        static_cast<uint8_t>(matrix_el->GetValue()));
                  }

                  auto* range_el = static_cast<KaxVideoColourRange*>(
                      colour->FindFirstElt(EBML_INFO(KaxVideoColourRange)));
                  if (range_el) {
                    color_space.set_range_from_uint8(
                        static_cast<uint8_t>(range_el->GetValue()));
                  }

                  auto* transfer_el =
                      static_cast<KaxVideoColourTransferCharacter*>(
                          colour->FindFirstElt(
                              EBML_INFO(KaxVideoColourTransferCharacter)));
                  if (transfer_el) {
                    color_space.set_transfer_from_uint8(
                        static_cast<uint8_t>(transfer_el->GetValue()));
                  }

                  auto* primaries_el = static_cast<KaxVideoColourPrimaries*>(
                      colour->FindFirstElt(EBML_INFO(KaxVideoColourPrimaries)));
                  if (primaries_el) {
                    color_space.set_primaries_from_uint8(
                        static_cast<uint8_t>(primaries_el->GetValue()));
                  }

                  // Handle HDR static metadata
                  media::HdrMetadata hdr_metadata;
                  bool has_hdr_metadata = false;

                  auto* max_cll_el = static_cast<KaxVideoColourMaxCLL*>(
                      colour->FindFirstElt(EBML_INFO(KaxVideoColourMaxCLL)));
                  if (max_cll_el) {
                    hdr_metadata.max_content_light_level =
                        static_cast<int32_t>(max_cll_el->GetValue());
                    has_hdr_metadata = true;
                  }

                  auto* max_fall_el = static_cast<KaxVideoColourMaxFALL*>(
                      colour->FindFirstElt(EBML_INFO(KaxVideoColourMaxFALL)));
                  if (max_fall_el) {
                    hdr_metadata.max_frame_average_light_level =
                        static_cast<int32_t>(max_fall_el->GetValue());
                    has_hdr_metadata = true;
                  }

                  auto* master_meta = static_cast<KaxVideoColourMasterMeta*>(
                      colour->FindFirstElt(
                          EBML_INFO(KaxVideoColourMasterMeta)));
                  if (master_meta) {
                    has_hdr_metadata = true;
                    auto* rx = static_cast<KaxVideoRChromaX*>(
                        master_meta->FindFirstElt(EBML_INFO(KaxVideoRChromaX)));
                    auto* ry = static_cast<KaxVideoRChromaY*>(
                        master_meta->FindFirstElt(EBML_INFO(KaxVideoRChromaY)));
                    auto* gx = static_cast<KaxVideoGChromaX*>(
                        master_meta->FindFirstElt(EBML_INFO(KaxVideoGChromaX)));
                    auto* gy = static_cast<KaxVideoGChromaY*>(
                        master_meta->FindFirstElt(EBML_INFO(KaxVideoGChromaY)));
                    auto* bx = static_cast<KaxVideoBChromaX*>(
                        master_meta->FindFirstElt(EBML_INFO(KaxVideoBChromaX)));
                    auto* by = static_cast<KaxVideoBChromaY*>(
                        master_meta->FindFirstElt(EBML_INFO(KaxVideoBChromaY)));
                    auto* wx = static_cast<KaxVideoWhitePointChromaX*>(
                        master_meta->FindFirstElt(
                            EBML_INFO(KaxVideoWhitePointChromaX)));
                    auto* wy = static_cast<KaxVideoWhitePointChromaY*>(
                        master_meta->FindFirstElt(
                            EBML_INFO(KaxVideoWhitePointChromaY)));
                    auto* lum_max = static_cast<KaxVideoLuminanceMax*>(
                        master_meta->FindFirstElt(
                            EBML_INFO(KaxVideoLuminanceMax)));
                    auto* lum_min = static_cast<KaxVideoLuminanceMin*>(
                        master_meta->FindFirstElt(
                            EBML_INFO(KaxVideoLuminanceMin)));

                    if (rx) {
                      hdr_metadata.mastering_metadata.primary_r.x =
                          static_cast<float>(rx->GetValue());
                    }
                    if (ry) {
                      hdr_metadata.mastering_metadata.primary_r.y =
                          static_cast<float>(ry->GetValue());
                    }
                    if (gx) {
                      hdr_metadata.mastering_metadata.primary_g.x =
                          static_cast<float>(gx->GetValue());
                    }
                    if (gy) {
                      hdr_metadata.mastering_metadata.primary_g.y =
                          static_cast<float>(gy->GetValue());
                    }
                    if (bx) {
                      hdr_metadata.mastering_metadata.primary_b.x =
                          static_cast<float>(bx->GetValue());
                    }
                    if (by) {
                      hdr_metadata.mastering_metadata.primary_b.y =
                          static_cast<float>(by->GetValue());
                    }
                    if (wx) {
                      hdr_metadata.mastering_metadata.white_point.x =
                          static_cast<float>(wx->GetValue());
                    }
                    if (wy) {
                      hdr_metadata.mastering_metadata.white_point.y =
                          static_cast<float>(wy->GetValue());
                    }
                    if (lum_max) {
                      hdr_metadata.mastering_metadata.luminance_max =
                          static_cast<float>(lum_max->GetValue());
                    }
                    if (lum_min) {
                      hdr_metadata.mastering_metadata.luminance_min =
                          static_cast<float>(lum_min->GetValue());
                    }
                  }

                  if (has_hdr_metadata) {
                    color_space.set_hdr_metadata(&hdr_metadata);
                  }

                  meta->SetColorSpace(color_space);
                }
              }
            } else if (media_type == MediaType::AUDIO) {
              // Audio parameters
              auto* audio = static_cast<KaxTrackAudio*>(
                  entry->FindFirstElt(EBML_INFO(KaxTrackAudio)));
              if (audio) {
                auto* freq_el = static_cast<KaxAudioSamplingFreq*>(
                    audio->FindFirstElt(EBML_INFO(KaxAudioSamplingFreq)));
                auto* chan_el = static_cast<KaxAudioChannels*>(
                    audio->FindFirstElt(EBML_INFO(KaxAudioChannels)));
                auto* depth_el = static_cast<KaxAudioBitDepth*>(
                    audio->FindFirstElt(EBML_INFO(KaxAudioBitDepth)));

                if (freq_el) {
                  meta->SetSampleRate(
                      static_cast<uint32_t>(freq_el->GetValue()));
                }
                if (chan_el) {
                  const auto channels = chan_el->GetValue();
                  meta->SetChannelLayout(
                      media::GuessChannelLayout(static_cast<int>(channels)));
                }
                if (depth_el) {
                  meta->SetBitsPerSample(depth_el->GetValue());
                }
              }
            }

            uint64_t def_dur_ns = 0;
            auto* def_dur_el = static_cast<KaxTrackDefaultDuration*>(
                entry->FindFirstElt(EBML_INFO(KaxTrackDefaultDuration)));
            if (def_dur_el) {
              def_dur_ns = def_dur_el->GetValue();
            }

            // MediaMeta stores track duration by media type. The container
            // format is MediaType::UNKNOWN, so preserve the segment duration
            // on each track for GenericSource to report to the player.
            if (duration_us_ > 0) {
              meta->SetDuration(base::TimeDelta::Micros(duration_us_));
            }

            size_t idx = tracks_.size();
            std::shared_ptr<MkvSource> source =
                std::make_shared<MkvSource>(this, idx, meta);
            tracks_.push_back(
                {track_num, idx, meta, media_type, {}, def_dur_ns, source});
          }
        }
      } else if (EbmlId(*child) == EBML_ID(KaxCues)) {
        auto* cues = static_cast<KaxCues*>(child);
        cues->ReadData(stream_->I_O(), SCOPE_ALL_DATA);
        ParseCues(cues);
      } else if (EbmlId(*child) == EBML_ID(KaxCluster)) {
        first_cluster_offset_ = child->GetElementPosition();
        stream_->I_O().setFilePointer(first_cluster_offset_);
        break;
      } else {
        child->SkipData(*stream_, EBML_CLASS_CONTEXT(KaxSegment));
      }

      delete child;
      child = stream_->FindNextElement(EBML_CLASS_CONTEXT(KaxSegment), upper,
                                       UINT64_MAX, true);
    }
  } catch (...) {
    AVE_LOG(LS_ERROR) << "MkvDemuxer::Init: Exception during init parsing";
    return media::ERROR_MALFORMED;
  }

  if (tracks_.empty()) {
    AVE_LOG(LS_ERROR) << "MkvDemuxer::Init: No valid tracks found";
    return media::ERROR_MALFORMED;
  }

  source_format_ =
      MediaMeta::CreatePtr(MediaType::UNKNOWN, MediaMeta::FormatType::kTrack);
  if (duration_us_ > 0) {
    source_format_->SetDuration(base::TimeDelta::Micros(duration_us_));
  }

  return OK;
}

status_t MkvDemuxer::GetFormat(std::shared_ptr<MediaMeta>& format) {
  format = source_format_;
  return OK;
}

size_t MkvDemuxer::GetTrackCount() {
  return tracks_.size();
}

status_t MkvDemuxer::GetTrackFormat(std::shared_ptr<MediaMeta>& format,
                                    size_t track_index) {
  if (track_index >= tracks_.size()) {
    return BAD_VALUE;
  }
  format = tracks_[track_index].meta;
  return OK;
}

std::shared_ptr<MediaSource> MkvDemuxer::GetTrack(size_t track_index) {
  if (track_index >= tracks_.size()) {
    return nullptr;
  }
  return tracks_[track_index].source;
}

status_t MkvDemuxer::ReadFrame(size_t track_index,
                               std::shared_ptr<MediaFrame>& frame,
                               const MediaSource::ReadOptions* options) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (track_index >= tracks_.size()) {
    return BAD_VALUE;
  }

  auto& track = tracks_[track_index];

  if (options) {
    int64_t seek_time_us = -1;
    MediaSource::ReadOptions::SeekMode mode;
    if (options->GetSeekTo(&seek_time_us, &mode)) {
      // 1. Clear all track packet queues
      for (auto& t : tracks_) {
        t.packet_queue.clear();
      }

      // 2. Perform seek to position
      uint64_t target_file_pos = first_cluster_offset_;
      if (!cues_.empty()) {
        auto it = std::upper_bound(cues_.begin(), cues_.end(),
                                   static_cast<uint64_t>(seek_time_us),
                                   [](uint64_t time, const MkvCue& cue) {
                                     return time < cue.time_us;
                                   });
        if (it != cues_.begin()) {
          --it;
          target_file_pos = it->file_pos;
        } else {
          target_file_pos = cues_.front().file_pos;
        }
      }

      stream_->I_O().setFilePointer(target_file_pos);
      if (current_element_) {
        delete current_element_;
        current_element_ = nullptr;
      }
      current_cluster_ = nullptr;
      upper_level_ = 0;
    }
  }

  while (track.packet_queue.empty()) {
    status_t err = ReadNextBlock();
    if (err != OK) {
      return err;
    }
  }

  frame = track.packet_queue.front();
  track.packet_queue.pop_front();
  AVE_LOG_IF(LS_VERBOSE, en_debug_log_)
      << "MkvDemuxer frame: track=" << track_index
      << " pts_us=" << frame->pts().us() << " dts_us=" << frame->dts().us()
      << " size=" << frame->size() << " queued=" << track.packet_queue.size();
  return OK;
}

status_t MkvDemuxer::ReadNextBlock() {
  while (true) {
    if (current_element_ == nullptr) {
      int upper = 0;
      current_element_ = stream_->FindNextElement(
          EBML_CLASS_CONTEXT(KaxSegment), upper, UINT64_MAX, true);
      if (current_element_ == nullptr) {
        return media::ERROR_END_OF_STREAM;
      }
      if (upper > 0) {
        delete current_element_;
        current_element_ = nullptr;
        return media::ERROR_END_OF_STREAM;
      }
    }

    if (EbmlId(*current_element_) == EBML_ID(KaxCluster)) {
      current_cluster_ = static_cast<KaxCluster*>(current_element_);

      int cluster_upper = 0;
      EbmlElement* cluster_child = stream_->FindNextElement(
          EBML_CLASS_CONTEXT(KaxCluster), cluster_upper, UINT64_MAX, true);

      while (cluster_child != nullptr) {
        if (cluster_upper > 0) {
          current_element_ = cluster_child;
          current_cluster_ = nullptr;
          upper_level_ = cluster_upper - 1;
          break;
        }

        if (EbmlId(*cluster_child) == EBML_ID(KaxClusterTimestamp)) {
          auto* tc = static_cast<KaxClusterTimestamp*>(cluster_child);
          tc->ReadData(stream_->I_O(), SCOPE_ALL_DATA);
          uint64_t cluster_timecode = tc->GetValue();
          current_cluster_->InitTimestamp(cluster_timecode, timecode_scale_);
        } else if (EbmlId(*cluster_child) == EBML_ID(KaxSimpleBlock)) {
          auto* sb = static_cast<KaxSimpleBlock*>(cluster_child);
          sb->ReadData(stream_->I_O(), SCOPE_ALL_DATA);
          sb->SetParent(*current_cluster_);
          EnqueueFramesFromBlock(*sb);

          delete cluster_child;
          return OK;
        } else if (EbmlId(*cluster_child) == EBML_ID(KaxBlockGroup)) {
          auto* bg = static_cast<KaxBlockGroup*>(cluster_child);
          bg->ReadData(stream_->I_O(), SCOPE_ALL_DATA);
          bg->SetParent(*current_cluster_);

          auto* block =
              static_cast<KaxBlock*>(bg->FindFirstElt(EBML_INFO(KaxBlock)));
          if (block) {
            auto* duration_el = static_cast<KaxBlockDuration*>(
                bg->FindFirstElt(EBML_INFO(KaxBlockDuration)));
            uint64_t duration = 0;
            if (duration_el) {
              duration = duration_el->GetValue();
            }
            EnqueueFramesFromBlock(*block, duration);
          }

          delete cluster_child;
          return OK;
        } else {
          cluster_child->SkipData(*stream_, EBML_CLASS_CONTEXT(KaxCluster));
        }

        delete cluster_child;
        cluster_child = stream_->FindNextElement(
            EBML_CLASS_CONTEXT(KaxCluster), cluster_upper, UINT64_MAX, true);
      }

      if (cluster_child == nullptr) {
        delete current_element_;
        current_element_ = nullptr;
        current_cluster_ = nullptr;
      }
    } else {
      current_element_->SkipData(*stream_, EBML_CLASS_CONTEXT(KaxSegment));
      delete current_element_;
      current_element_ = nullptr;
    }
  }
}

void MkvDemuxer::EnqueueFramesFromBlock(KaxInternalBlock& block,
                                        uint64_t duration_scale_units) {
  uint32_t track_num = block.TrackNum();
  MkvTrack* track = FindTrackByNumber(track_num);
  if (!track) {
    return;
  }

  unsigned int num_frames = block.NumberFrames();
  uint64_t block_timestamp_ns = block.GlobalTimestamp();

  uint64_t default_duration_ns = track->default_duration_ns;
  uint64_t frame_duration_ns = default_duration_ns;

  if (duration_scale_units > 0 && num_frames > 0) {
    frame_duration_ns = (duration_scale_units * timecode_scale_) / num_frames;
  }

  for (unsigned int i = 0; i < num_frames; ++i) {
    DataBuffer& buf = block.GetBuffer(i);
    uint32_t size = buf.Size();
    const uint8_t* data = buf.Buffer();
    if (size == 0 || data == nullptr) {
      continue;
    }

    std::shared_ptr<MediaFrame> frame =
        MediaFrame::CreateShared(size, track->type);
    if (!frame) {
      continue;
    }
    memcpy(frame->data(), data, size);
    frame->setRange(0, size);

    uint64_t frame_pts_ns = block_timestamp_ns + i * frame_duration_ns;
    frame->SetPts(base::Timestamp::Micros(frame_pts_ns / 1000));
    frame->SetDts(base::Timestamp::Micros(frame_pts_ns / 1000));
    frame->SetDuration(base::TimeDelta::Micros(frame_duration_ns / 1000));
    frame->SetCodec(track->meta->codec());
    frame->SetStreamType(track->type);

    track->packet_queue.push_back(std::move(frame));
  }
}

void MkvDemuxer::ParseCues(KaxCues* cues) {
  cues_.clear();
  const std::vector<EbmlElement*>& cue_points = cues->GetElementList();
  for (EbmlElement* el : cue_points) {
    if (EbmlId(*el) == EBML_ID(KaxCuePoint)) {
      auto* pt = static_cast<KaxCuePoint*>(el);
      auto* cue_time_el =
          static_cast<KaxCueTime*>(pt->FindFirstElt(EBML_INFO(KaxCueTime)));
      auto* track_pos_el = static_cast<KaxCueTrackPositions*>(
          pt->FindFirstElt(EBML_INFO(KaxCueTrackPositions)));
      if (cue_time_el && track_pos_el) {
        auto* cluster_pos_el = static_cast<KaxCueClusterPosition*>(
            track_pos_el->FindFirstElt(EBML_INFO(KaxCueClusterPosition)));
        if (cluster_pos_el) {
          uint64_t raw_time = cue_time_el->GetValue();
          uint64_t cue_time_us = (raw_time * timecode_scale_) / 1000;
          uint64_t rel_pos = cluster_pos_el->GetValue();
          uint64_t file_pos = segment_->GetGlobalPosition(rel_pos);
          cues_.push_back({cue_time_us, file_pos});
        }
      }
    }
  }

  std::sort(cues_.begin(), cues_.end(), [](const MkvCue& a, const MkvCue& b) {
    return a.time_us < b.time_us;
  });
}

MkvDemuxer::MkvTrack* MkvDemuxer::FindTrackByNumber(uint32_t track_num) {
  for (auto& track : tracks_) {
    if (track.track_num == track_num) {
      return &track;
    }
  }
  return nullptr;
}

}  // namespace player
}  // namespace ave
