/*
 * internal_demuxer_factory.cc
 * Copyright (C) 2026 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "internal_demuxer_factory.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/logging.h"
#include "matroska/mkv_demuxer.h"
#include "mp4_demuxer.h"
#include "mpeg2/mpeg2_ps_demuxer.h"
#include "mpeg2/mpeg2_ts_demuxer.h"

namespace ave {
namespace player {

namespace {

using SupportedMimeTypes = std::vector<std::string>;

struct DemuxerCandidate {
  const char* name;
  std::optional<DemuxerSniffResult> (*sniff)(
      const std::shared_ptr<DataSource>& data_source);
  SupportedMimeTypes (*supported_mime_types)();
  std::shared_ptr<Demuxer> (*create_and_init)(
      const std::shared_ptr<DataSource>& data_source);
};

std::string NormalizeMimeType(std::string_view mime_type) {
  const size_t parameter_start = mime_type.find(';');
  mime_type = mime_type.substr(0, parameter_start);

  const size_t first = mime_type.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) {
    return {};
  }
  const size_t last = mime_type.find_last_not_of(" \t\r\n");

  std::string normalized(mime_type.substr(first, last - first + 1));
  std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  return normalized;
}

bool SupportsMimeType(const DemuxerCandidate& candidate,
                      std::string_view preferred_mime_type) {
  const std::string normalized_preferred =
      NormalizeMimeType(preferred_mime_type);
  if (normalized_preferred.empty()) {
    return false;
  }

  for (const std::string& mime_type : candidate.supported_mime_types()) {
    if (NormalizeMimeType(mime_type) == normalized_preferred) {
      return true;
    }
  }
  return false;
}

void ResetDataSourceForInit(const std::shared_ptr<DataSource>& data_source) {
  if (!data_source || !(data_source->Flags() & DataSource::kSeekable)) {
    return;
  }
  if (data_source->Seek(0, SEEK_SET) < 0) {
    AVE_LOG(LS_WARNING) << "InternalDemuxerFactory: failed to reset data "
                        << "source before demuxer initialization";
  }
}

template <typename DemuxerType>
std::shared_ptr<Demuxer> CreateAndInit(
    const std::shared_ptr<DataSource>& data_source) {
  ResetDataSourceForInit(data_source);
  auto demuxer = std::make_shared<DemuxerType>(data_source);
  const status_t error = demuxer->Init();
  if (error != OK) {
    AVE_LOG(LS_WARNING) << "InternalDemuxerFactory: " << demuxer->name()
                        << " initialization failed: " << error;
    return nullptr;
  }
  return demuxer;
}

const std::array<DemuxerCandidate, 4>& GetCandidates() {
  static const std::array<DemuxerCandidate, 4> candidates = {{
      {"Mpeg2TsDemuxer", &Mpeg2TsDemuxer::Sniff,
       &Mpeg2TsDemuxer::SupportedContainerMimeTypes,
       &CreateAndInit<Mpeg2TsDemuxer>},
      {"Mpeg2PsDemuxer", &Mpeg2PsDemuxer::Sniff,
       &Mpeg2PsDemuxer::SupportedContainerMimeTypes,
       &CreateAndInit<Mpeg2PsDemuxer>},
      {"Mp4Demuxer", &Mp4Demuxer::Sniff,
       &Mp4Demuxer::SupportedContainerMimeTypes, &CreateAndInit<Mp4Demuxer>},
      {"MkvDemuxer", &MkvDemuxer::Sniff,
       &MkvDemuxer::SupportedContainerMimeTypes, &CreateAndInit<MkvDemuxer>},
  }};
  return candidates;
}

}  // namespace

std::shared_ptr<Demuxer> InternalDemuxerFactory::CreateDemuxer(
    std::shared_ptr<DataSource> data_source) {
  return CreateDemuxer(std::move(data_source), {});
}

std::shared_ptr<Demuxer> InternalDemuxerFactory::CreateDemuxer(
    std::shared_ptr<DataSource> data_source,
    const std::vector<std::string>& preferred_container_mime_types) {
  if (!data_source) {
    return nullptr;
  }

  const auto& candidates = GetCandidates();
  std::vector<bool> attempted(candidates.size(), false);

  const auto try_candidate = [&](size_t index) -> std::shared_ptr<Demuxer> {
    attempted[index] = true;
    const DemuxerCandidate& candidate = candidates[index];
    const auto sniff_result = candidate.sniff(data_source);
    if (!sniff_result) {
      return nullptr;
    }

    auto demuxer = candidate.create_and_init(data_source);
    if (demuxer) {
      AVE_LOG(LS_INFO) << "InternalDemuxerFactory: using " << candidate.name;
      AVE_LOG(LS_VERBOSE) << "InternalDemuxerFactory: detected container "
                          << sniff_result->container_mime_type;
    }
    return demuxer;
  };

  for (const std::string& mime_type : preferred_container_mime_types) {
    for (size_t index = 0; index < candidates.size(); ++index) {
      if (!attempted[index] && SupportsMimeType(candidates[index], mime_type)) {
        if (auto demuxer = try_candidate(index)) {
          return demuxer;
        }
      }
    }
  }

  for (size_t index = 0; index < candidates.size(); ++index) {
    if (!attempted[index]) {
      if (auto demuxer = try_candidate(index)) {
        return demuxer;
      }
    }
  }

  return nullptr;
}

}  // namespace player
}  // namespace ave
