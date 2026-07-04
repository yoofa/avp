/*
 * rtsp_source.cc
 * Copyright (C) 2026 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "content_source/rtsp/rtsp_source.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstring>
#include <optional>
#include <ranges>
#include <sstream>
#include <utility>

#include "base/logging.h"
#include "base/net/async_tcp_socket.h"
#include "base/net/async_udp_socket.h"
#include "base/task_util/default_task_runner_factory.h"
#include "base/task_util/task_runner.h"
#include "media/audio/channel_layout.h"
#include "media/codec/codec_id.h"
#include "media/foundation/media_mimes.h"
#include "media/modules/rtp_rtcp/src/rtp/rtp_packet_received.h"
#include "media/modules/rtp_rtcp/src/video/video_rtp_depacketizer_h264.h"
#include "media/modules/rtp_rtcp/src/video/video_rtp_depacketizer_h265.h"

namespace ave::player {
namespace {

constexpr int64_t kInitialBufferUs = 500'000;
constexpr int64_t kUnderflowBufferUs = 200'000;
constexpr int64_t kResumeBufferUs = 800'000;
constexpr uint64_t kMaintenancePeriodUs = 100'000;
constexpr uint64_t kKeepAlivePeriodUs = 20'000'000;
constexpr uint64_t kUdpFallbackUs = 2'000'000;
constexpr size_t kMaxAccessUnitSize = 2U * 1024U * 1024U;

std::string ToLower(std::string value) {
  std::ranges::transform(value, value.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return value;
}

std::string Trim(std::string value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return {};
  }
  const auto last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1U);
}

std::string Base64Encode(const std::string& value) {
  static constexpr std::string_view kAlphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string result;
  for (size_t offset = 0; offset < value.size(); offset += 3U) {
    const uint32_t first = static_cast<uint8_t>(value[offset]);
    const uint32_t second = offset + 1U < value.size()
                                ? static_cast<uint8_t>(value[offset + 1U])
                                : 0U;
    const uint32_t third = offset + 2U < value.size()
                               ? static_cast<uint8_t>(value[offset + 2U])
                               : 0U;
    const uint32_t packed = (first << 16U) | (second << 8U) | third;
    result.push_back(kAlphabet[(packed >> 18U) & 0x3fU]);
    result.push_back(kAlphabet[(packed >> 12U) & 0x3fU]);
    result.push_back(offset + 1U < value.size() ? kAlphabet[(packed >> 6U) & 0x3fU]
                                                 : '=');
    result.push_back(offset + 2U < value.size() ? kAlphabet[packed & 0x3fU] : '=');
  }
  return result;
}

std::vector<uint8_t> Base64Decode(const std::string& value) {
  static constexpr std::string_view kAlphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::vector<uint8_t> result;
  uint32_t packed = 0;
  uint32_t bits = 0;
  for (const char character : value) {
    if (character == '=') break;
    const size_t index = kAlphabet.find(character);
    if (index == std::string::npos) return {};
    packed = (packed << 6U) | static_cast<uint32_t>(index);
    bits += 6U;
    if (bits >= 8U) {
      bits -= 8U;
      result.push_back(static_cast<uint8_t>(packed >> bits));
    }
  }
  return result;
}

std::vector<uint8_t> MakeAnnexBConfig(const std::vector<std::string>& values) {
  static constexpr std::array<uint8_t, 4> kStartCode = {0, 0, 0, 1};
  std::vector<uint8_t> result;
  for (const auto& value : values) {
    const auto nalu = Base64Decode(value);
    if (nalu.empty()) continue;
    result.insert(result.end(), kStartCode.begin(), kStartCode.end());
    result.insert(result.end(), nalu.begin(), nalu.end());
  }
  return result;
}

uint32_t RotateLeft(uint32_t value, uint32_t bits) {
  return (value << bits) | (value >> (32U - bits));
}

// RFC 1321 MD5, kept local because RTSP Digest is the only crypto consumer in
// this layer and linking the platform crypto implementation is not portable.
std::string Md5Hex(const std::string& input) {
  static constexpr std::array<uint32_t, 64> kShifts = {
      7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
      5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
      4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
      6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
  static constexpr std::array<uint32_t, 64> kConstants = {
      0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
      0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
      0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
      0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
      0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
      0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
      0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
      0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
  std::vector<uint8_t> bytes(input.begin(), input.end());
  const uint64_t bit_count = static_cast<uint64_t>(bytes.size()) * 8U;
  bytes.push_back(0x80U);
  while (bytes.size() % 64U != 56U) {
    bytes.push_back(0U);
  }
  for (uint32_t index = 0; index < 8U; ++index) {
    bytes.push_back(static_cast<uint8_t>(bit_count >> (index * 8U)));
  }
  uint32_t a0 = 0x67452301U;
  uint32_t b0 = 0xefcdab89U;
  uint32_t c0 = 0x98badcfeU;
  uint32_t d0 = 0x10325476U;
  for (size_t offset = 0; offset < bytes.size(); offset += 64U) {
    std::array<uint32_t, 16> words{};
    for (uint32_t index = 0; index < words.size(); ++index) {
      words[index] = static_cast<uint32_t>(bytes[offset + index * 4U]) |
                     (static_cast<uint32_t>(bytes[offset + index * 4U + 1U]) << 8U) |
                     (static_cast<uint32_t>(bytes[offset + index * 4U + 2U]) << 16U) |
                     (static_cast<uint32_t>(bytes[offset + index * 4U + 3U]) << 24U);
    }
    uint32_t a = a0;
    uint32_t b = b0;
    uint32_t c = c0;
    uint32_t d = d0;
    for (uint32_t index = 0; index < 64U; ++index) {
      uint32_t function = 0;
      uint32_t word_index = 0;
      if (index < 16U) { function = (b & c) | (~b & d); word_index = index; }
      else if (index < 32U) { function = (d & b) | (~d & c); word_index = (5U * index + 1U) % 16U; }
      else if (index < 48U) { function = b ^ c ^ d; word_index = (3U * index + 5U) % 16U; }
      else { function = c ^ (b | ~d); word_index = (7U * index) % 16U; }
      const uint32_t next = d;
      d = c;
      c = b;
      b += RotateLeft(a + function + kConstants[index] + words[word_index], kShifts[index]);
      a = next;
    }
    a0 += a; b0 += b; c0 += c; d0 += d;
  }
  std::array<uint8_t, 16> digest{};
  const std::array<uint32_t, 4> words = {a0, b0, c0, d0};
  for (uint32_t index = 0; index < words.size(); ++index) {
    for (uint32_t byte = 0; byte < 4U; ++byte) {
      digest[index * 4U + byte] = static_cast<uint8_t>(words[index] >> (byte * 8U));
    }
  }
  static constexpr std::string_view kHex = "0123456789abcdef";
  std::string result;
  result.reserve(32U);
  for (uint8_t byte : digest) {
    result.push_back(kHex[byte >> 4U]);
    result.push_back(kHex[byte & 0x0fU]);
  }
  return result;
}

std::unordered_map<std::string, std::string> ParseAuthParameters(
    const std::string& value) {
  std::unordered_map<std::string, std::string> parameters;
  size_t offset = 0;
  while (offset < value.size()) {
    const size_t equal = value.find('=', offset);
    if (equal == std::string::npos) break;
    const std::string key = ToLower(Trim(value.substr(offset, equal - offset)));
    size_t value_begin = equal + 1U;
    size_t value_end = value_begin;
    if (value_begin < value.size() && value[value_begin] == '\"') {
      ++value_begin;
      value_end = value.find('\"', value_begin);
      if (value_end == std::string::npos) break;
      offset = value_end + 1U;
    } else {
      value_end = value.find(',', value_begin);
      if (value_end == std::string::npos) value_end = value.size();
      offset = value_end;
    }
    parameters.emplace(key, Trim(value.substr(value_begin, value_end - value_begin)));
    if (offset < value.size() && value[offset] == ',') ++offset;
    while (offset < value.size() && std::isspace(static_cast<unsigned char>(value[offset]))) ++offset;
  }
  return parameters;
}

std::unordered_map<std::string, std::string> ParseHeaders(
    const std::string& response) {
  std::unordered_map<std::string, std::string> headers;
  size_t offset = response.find("\r\n");
  while (offset != std::string::npos) {
    const size_t end = response.find("\r\n", offset + 2U);
    if (end == std::string::npos || end == offset + 2U) {
      break;
    }
    const std::string line = response.substr(offset + 2U, end - offset - 2U);
    const size_t colon = line.find(':');
    if (colon != std::string::npos) {
      headers.emplace(ToLower(Trim(line.substr(0, colon))),
                      Trim(line.substr(colon + 1U)));
    }
    offset = end;
  }
  return headers;
}

int32_t ResponseStatus(const std::string& response) {
  const size_t first_space = response.find(' ');
  if (first_space == std::string::npos) {
    return 0;
  }
  int32_t status = 0;
  const char* begin = response.data() + first_space + 1U;
  const char* end = response.data() + response.find(' ', first_space + 1U);
  std::from_chars(begin, end, status);
  return status;
}

std::vector<uint8_t> HexToBytes(const std::string& value) {
  if (value.size() % 2U != 0U) {
    return {};
  }
  std::vector<uint8_t> bytes(value.size() / 2U);
  for (size_t index = 0; index < bytes.size(); ++index) {
    const auto [ptr, error] = std::from_chars(value.data() + index * 2U,
                                              value.data() + index * 2U + 2U,
                                              bytes[index], 16);
    if (error != std::errc{} || ptr != value.data() + index * 2U + 2U) {
      return {};
    }
  }
  return bytes;
}

media::ChannelLayout ChannelLayoutForCount(uint32_t channels) {
  return media::GuessChannelLayout(static_cast<int>(channels));
}

}  // namespace

struct RTSPSource::Track {
  media::MediaType type = media::MediaType::UNKNOWN;
  uint8_t payload_type = 0;
  uint32_t clock_rate = 90'000;
  uint32_t channels = 0;
  std::string encoding;
  std::string control_uri;
  std::unordered_map<std::string, std::string> fmtp;
  std::shared_ptr<media::MediaMeta> format;
  std::shared_ptr<PacketSource> packet_source;
  std::unique_ptr<base::net::AsyncPacketSocket> rtp_socket;
  std::unique_ptr<base::net::AsyncPacketSocket> rtcp_socket;
  std::unique_ptr<media::rtp_rtcp::VideoRtpDepacketizer> depacketizer;
  std::vector<uint8_t> frame_data;
  uint32_t frame_timestamp = 0;
  bool have_frame_timestamp = false;
  bool frame_is_key = false;
  bool waiting_for_keyframe = true;
  bool have_rtp_timestamp = false;
  uint32_t first_rtp_timestamp = 0;
  int64_t last_pts_us = 0;
  int64_t first_packet_time_us = 0;
  int64_t last_packet_time_us = 0;
  uint8_t rtp_channel = 0;
  uint8_t rtcp_channel = 1;
};

RTSPSource::RTSPSource() {
  auto factory = base::CreateDefaultTaskRunnerFactory();
  timer_task_runner_ = std::make_unique<base::TaskRunner>(
      factory->CreateTaskRunner("RTSPSourceTimer",
                                base::TaskRunnerFactory::Priority::NORMAL));
}

RTSPSource::~RTSPSource() {
  maintenance_task_.Stop();
  network_thread_.PostTask([this] {
    std::scoped_lock lock(lock_);
    CloseOnNetworkThread(false);
  });
  network_thread_.Stop();
}

status_t RTSPSource::SetDataSource(
    const char* url,
    const std::unordered_map<std::string, std::string>& headers) {
  if (!url) {
    return BAD_VALUE;
  }
  std::scoped_lock lock(lock_);
  if (state_ != State::kIdle) {
    return INVALID_OPERATION;
  }
  url_ = url;
  headers_ = headers;
  for (const auto& [name, value] : headers_) {
    if (ToLower(name) == "authorization") {
      authentication_header_ = value;
      break;
    }
  }
  return ParseUrlLocked() ? OK : BAD_VALUE;
}

void RTSPSource::SetNotify(Notify* notify) {
  std::scoped_lock lock(lock_);
  notify_ = notify;
}

void RTSPSource::Prepare() {
  uint64_t generation = 0;
  {
    std::scoped_lock lock(lock_);
    if (url_.empty() || state_ != State::kIdle) {
      return;
    }
    generation = ++generation_;
    state_ = State::kConnecting;
  }
  network_thread_.Start();
  network_thread_.PostTask([self = shared_from_this(), generation] {
    self->PrepareOnNetworkThread(generation);
  });
}

void RTSPSource::Start() {
  std::scoped_lock lock(lock_);
  started_ = true;
  dequeue_ready_.store(!buffering_, std::memory_order_release);
}

void RTSPSource::Stop() {
  uint64_t generation = 0;
  {
    std::scoped_lock lock(lock_);
    if (state_ == State::kIdle || state_ == State::kStopping) {
      return;
    }
    state_ = State::kStopping;
    started_ = false;
    dequeue_ready_.store(false, std::memory_order_release);
    generation = ++generation_;
  }
  network_thread_.PostTask([self = shared_from_this(), generation] {
    std::scoped_lock lock(self->lock_);
    if (generation != self->generation_) {
      return;
    }
    self->CloseOnNetworkThread(true);
    self->state_ = State::kIdle;
  });
}

void RTSPSource::Pause() {
  network_thread_.PostTask([self = shared_from_this()] {
    std::scoped_lock lock(self->lock_);
    if (self->state_ != State::kPlaying) {
      return;
    }
    self->SendRequest("PAUSE", self->base_uri_);
    self->state_ = State::kPaused;
    self->started_ = false;
    self->dequeue_ready_.store(false, std::memory_order_release);
  });
}

void RTSPSource::Resume() {
  network_thread_.PostTask([self = shared_from_this()] {
    std::scoped_lock lock(self->lock_);
    if (self->state_ != State::kPaused) {
      return;
    }
    self->ResetMediaLocked();
    self->buffering_ = true;
    self->started_ = true;
    self->SendRequest("PLAY", self->base_uri_);
    self->state_ = State::kPlay;
  });
}

status_t RTSPSource::DequeueAccessUnit(
    media::MediaType track_type,
    std::shared_ptr<media::MediaFrame>& access_unit) {
  access_unit.reset();
  if (!dequeue_ready_.load(std::memory_order_acquire)) {
    return WOULD_BLOCK;
  }
  std::shared_ptr<PacketSource> source;
  {
    std::scoped_lock lock(lock_);
    source = track_type == media::MediaType::AUDIO ? audio_packet_source_
                                                    : video_packet_source_;
  }
  if (!source) {
    return WOULD_BLOCK;
  }
  status_t result = OK;
  if (!source->HasBufferAvailable(&result)) {
    return result == OK ? WOULD_BLOCK : result;
  }
  return source->DequeueAccessUnit(access_unit);
}

std::shared_ptr<media::MediaMeta> RTSPSource::GetFormat() {
  std::scoped_lock lock(lock_);
  return source_format_;
}

status_t RTSPSource::GetDuration(int64_t* duration_us) {
  if (!duration_us) {
    return BAD_VALUE;
  }
  *duration_us = -1;
  return INVALID_OPERATION;
}

size_t RTSPSource::GetTrackCount() const {
  std::scoped_lock lock(lock_);
  return tracks_.size();
}

std::shared_ptr<media::MediaMeta> RTSPSource::GetTrackInfo(size_t track_index) const {
  std::scoped_lock lock(lock_);
  return track_index < tracks_.size() ? tracks_[track_index].format : nullptr;
}

std::shared_ptr<media::MediaMeta> RTSPSource::GetTrackInfo(
    media::MediaType track_type) const {
  std::scoped_lock lock(lock_);
  for (const auto& track : tracks_) {
    if (track.type == track_type) {
      return track.format;
    }
  }
  return nullptr;
}

status_t RTSPSource::SelectTrack(size_t track_index, bool select) {
  std::scoped_lock lock(lock_);
  return select && track_index < tracks_.size() ? OK : INVALID_OPERATION;
}

bool RTSPSource::IsStreaming() const {
  return true;
}

void RTSPSource::PrepareOnNetworkThread(uint64_t generation) {
  std::scoped_lock lock(lock_);
  if (generation != generation_) {
    return;
  }
  ConnectOnNetworkThread(generation);
}

void RTSPSource::ConnectOnNetworkThread(uint64_t /* generation */) {
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* result = nullptr;
  if (getaddrinfo(host_.c_str(), std::to_string(port_).c_str(), &hints,
                  &result) != 0 || result == nullptr) {
    NotifyError(NAME_NOT_FOUND);
    return;
  }
  const auto* address = reinterpret_cast<const sockaddr_in*>(result->ai_addr);
  const uint32_t ip = ntohl(address->sin_addr.s_addr);
  freeaddrinfo(result);
  auto* socket = network_thread_.CreateSocket(AF_INET, SOCK_STREAM);
  if (!socket) {
    NotifyError(NO_INIT);
    return;
  }
  auto tcp = base::net::AsyncTCPSocket::Create(
      socket, base::net::EmptySocketAddressWithFamily(AF_INET),
      base::net::SocketAddress(ip, port_));
  if (!tcp) {
    NotifyError(NO_INIT);
    return;
  }
  rtsp_socket_.reset(tcp);
  rtsp_socket_->SignalConnect.connect(this, &RTSPSource::OnSocketConnected);
  rtsp_socket_->SignalReadPacket.connect(this, &RTSPSource::OnTcpRead);
  rtsp_socket_->SignalClose.connect(this, &RTSPSource::OnSocketClosed);
}

void RTSPSource::OnSocketConnected(base::net::AsyncPacketSocket* socket) {
  std::scoped_lock lock(lock_);
  if (socket != rtsp_socket_.get() || state_ != State::kConnecting) {
    return;
  }
  SendRequest("OPTIONS", base_uri_);
  state_ = State::kOptions;
}

void RTSPSource::SendRequest(const std::string& method,
                             const std::string& uri,
                             const std::vector<std::string>& extra_headers) {
  if (!rtsp_socket_) {
    return;
  }
  pending_method_ = method;
  pending_uri_ = uri;
  pending_extra_headers_ = extra_headers;
  std::ostringstream request;
  request << method << ' ' << uri << " RTSP/1.0\r\nCSeq: " << cseq_++
          << "\r\nUser-Agent: avp\r\n";
  if (!session_id_.empty()) {
    request << "Session: " << session_id_ << "\r\n";
  }
  if (!authentication_header_.empty()) {
    request << "Authorization: " << authentication_header_ << "\r\n";
  }
  for (const auto& [name, value] : headers_) {
    if (ToLower(name) != "authorization") {
      request << name << ": " << value << "\r\n";
    }
  }
  for (const auto& header : extra_headers) {
    request << header << "\r\n";
  }
  request << "\r\n";
  const std::string serialized = request.str();
  if (rtsp_socket_->Send(serialized.data(), serialized.size()) < 0) {
    NotifyError(DEAD_OBJECT);
  }
}

void RTSPSource::OnTcpRead(base::net::AsyncPacketSocket* socket,
                           const uint8_t* data,
                           size_t size,
                           const base::net::SocketAddress& /* remote_addr */,
                           int64_t /* timestamp_us */) {
  std::scoped_lock lock(lock_);
  if (socket != rtsp_socket_.get()) {
    return;
  }
  read_buffer_.append(reinterpret_cast<const char*>(data), size);
  while (!read_buffer_.empty()) {
    if (read_buffer_[0] == '$') {
      if (read_buffer_.size() < 4U) {
        return;
      }
      const size_t packet_size =
          (static_cast<uint8_t>(read_buffer_[2]) << 8U) |
          static_cast<uint8_t>(read_buffer_[3]);
      if (read_buffer_.size() < packet_size + 4U) {
        return;
      }
      HandleInterleavedData(static_cast<uint8_t>(read_buffer_[1]),
                            reinterpret_cast<const uint8_t*>(read_buffer_.data() + 4U),
                            packet_size);
      read_buffer_.erase(0, packet_size + 4U);
      continue;
    }
    const size_t header_end = read_buffer_.find("\r\n\r\n");
    if (header_end == std::string::npos) {
      return;
    }
    const std::string header = read_buffer_.substr(0, header_end + 4U);
    const auto headers = ParseHeaders(header);
    size_t content_length = 0;
    if (const auto it = headers.find("content-length"); it != headers.end()) {
      std::from_chars(it->second.data(), it->second.data() + it->second.size(),
                      content_length);
    }
    if (read_buffer_.size() < header_end + 4U + content_length) {
      return;
    }
    const std::string response =
        read_buffer_.substr(0, header_end + 4U + content_length);
    read_buffer_.erase(0, header_end + 4U + content_length);
    HandleRtspResponse(response);
  }
}

void RTSPSource::HandleRtspResponse(const std::string& response) {
  const int32_t status = ResponseStatus(response);
  const auto headers = ParseHeaders(response);
  if (status == 401) {
    if (const auto it = headers.find("www-authenticate");
        it != headers.end() && BuildAuthenticationLocked(it->second)) {
      SendRequest(pending_method_, pending_uri_, pending_extra_headers_);
      return;
    }
    NotifyError(PERMISSION_DENIED);
    return;
  }
  if (status < 200 || status >= 300) {
    NotifyError(UNKNOWN_ERROR);
    return;
  }
  if (const auto it = headers.find("session"); it != headers.end()) {
    session_id_ = Trim(it->second.substr(0, it->second.find(';')));
  }
  switch (state_) {
    case State::kOptions:
      SendRequest("DESCRIBE", base_uri_, {"Accept: application/sdp"});
      state_ = State::kDescribe;
      return;
    case State::kDescribe: {
      const size_t body_offset = response.find("\r\n\r\n");
      if (body_offset == std::string::npos || !ParseSdp(response.substr(body_offset + 4U))) {
        NotifyError(BAD_VALUE);
        return;
      }
      setup_track_index_ = 0;
      ContinueSetup();
      return;
    }
    case State::kSetup:
      ++setup_track_index_;
      ContinueSetup();
      return;
    case State::kPlay:
      state_ = State::kPlaying;
      return;
    default:
      return;
  }
}

void RTSPSource::ContinueSetup() {
  if (setup_track_index_ >= tracks_.size()) {
    StartPlayback();
    return;
  }
  Track& track = tracks_[setup_track_index_];
  if (transport_ == Transport::kUdp) {
    auto make_socket = [this]() -> std::unique_ptr<base::net::AsyncPacketSocket> {
      auto* raw = base::net::AsyncUDPSocket::Create(
          network_thread_.CreateSocket(AF_INET, SOCK_DGRAM),
          base::net::EmptySocketAddressWithFamily(AF_INET));
      return std::unique_ptr<base::net::AsyncPacketSocket>(raw);
    };
    track.rtp_socket = make_socket();
    track.rtcp_socket = make_socket();
    if (!track.rtp_socket || !track.rtcp_socket) {
      transport_ = Transport::kTcpInterleaved;
      state_ = State::kConnecting;
      CloseOnNetworkThread(false);
      ConnectOnNetworkThread(generation_);
      return;
    }
    track.rtp_socket->SignalReadPacket.connect(this, &RTSPSource::OnUdpRead);
    track.rtcp_socket->SignalReadPacket.connect(this, &RTSPSource::OnUdpRead);
    const uint16_t rtp_port = track.rtp_socket->GetLocalAddress().port();
    const uint16_t rtcp_port = track.rtcp_socket->GetLocalAddress().port();
    SendRequest("SETUP", track.control_uri,
                {"Transport: RTP/AVP;unicast;client_port=" +
                 std::to_string(rtp_port) + "-" + std::to_string(rtcp_port)});
  } else {
    track.rtp_channel = static_cast<uint8_t>(setup_track_index_ * 2U);
    track.rtcp_channel = static_cast<uint8_t>(track.rtp_channel + 1U);
    SendRequest("SETUP", track.control_uri,
                {"Transport: RTP/AVP/TCP;unicast;interleaved=" +
                 std::to_string(track.rtp_channel) + "-" +
                 std::to_string(track.rtcp_channel)});
  }
  state_ = State::kSetup;
}

void RTSPSource::StartPlayback() {
  SendRequest("PLAY", base_uri_);
  state_ = State::kPlay;
  const uint64_t generation = generation_;
  maintenance_task_ = base::RepeatingTaskHandle::Start(
      timer_task_runner_->Get(), [weak = weak_from_this(), generation]() -> uint64_t {
        if (const auto self = weak.lock()) {
          self->network_thread_.PostTask([self, generation] {
            std::scoped_lock lock(self->lock_);
            if (generation == self->generation_ && self->state_ != State::kIdle) {
              self->CheckBuffering();
            }
          });
        }
        return kMaintenancePeriodUs;
      });
}

void RTSPSource::OnUdpRead(base::net::AsyncPacketSocket* socket,
                           const uint8_t* data,
                           size_t size,
                           const base::net::SocketAddress& /* remote_addr */,
                           int64_t /* timestamp_us */) {
  std::scoped_lock lock(lock_);
  Track* track = FindTrackBySocket(socket);
  if (track && track->rtp_socket.get() == socket) {
    HandleRtpPacket(*track, data, size);
  }
}

void RTSPSource::HandleInterleavedData(uint8_t channel,
                                       const uint8_t* data,
                                       size_t size) {
  Track* track = FindTrackByChannel(channel);
  if (track && track->rtp_channel == channel) {
    HandleRtpPacket(*track, data, size);
  }
}

void RTSPSource::HandleRtpPacket(Track& track,
                                 const uint8_t* data,
                                 size_t size) {
  media::rtp_rtcp::RtpPacketReceived packet;
  if (!packet.Parse(data, size) || packet.PayloadType() != track.payload_type) {
    return;
  }
  udp_no_packet_ticks_ = 0;
  if (track.first_packet_time_us == 0) {
    track.first_packet_time_us = base::Timestamp::Micros(0).us();
  }
  if (track.type == media::MediaType::VIDEO) {
    QueueVideo(track, packet);
  } else {
    QueueAac(track, packet);
  }
}

void RTSPSource::QueueVideo(Track& track,
                            const media::rtp_rtcp::RtpPacketReceived& packet) {
  if (!track.depacketizer) {
    return;
  }
  if (!track.have_frame_timestamp || track.frame_timestamp != packet.Timestamp()) {
    track.frame_data.clear();
    track.frame_timestamp = packet.Timestamp();
    track.have_frame_timestamp = true;
    track.frame_is_key = false;
  }
  auto parsed = track.depacketizer->Parse(packet.PayloadBuffer());
  if (!parsed || parsed->video_payload.empty()) {
    return;
  }
  const auto payload = parsed->video_payload;
  if (track.frame_data.size() + payload.size() > kMaxAccessUnitSize) {
    track.frame_data.clear();
    track.waiting_for_keyframe = true;
    track.frame_is_key = false;
    return;
  }
  static constexpr std::array<uint8_t, 4> kStartCode = {0, 0, 0, 1};
  const bool has_start_code = payload.size() >= kStartCode.size() &&
                              std::equal(kStartCode.begin(), kStartCode.end(),
                                         payload.cdata());
  if (!has_start_code) {
    track.frame_data.insert(track.frame_data.end(), kStartCode.begin(),
                            kStartCode.end());
  }
  track.frame_data.insert(track.frame_data.end(), payload.cdata(),
                          payload.cdata() + payload.size());
  track.frame_is_key =
      track.frame_is_key ||
      parsed->video_header.frame_type == media::VideoFrameType::kVideoFrameKey;
  if (!packet.Marker()) {
    return;
  }
  const bool key_frame = track.frame_is_key;
  if (track.waiting_for_keyframe && !key_frame) {
    track.frame_data.clear();
    track.frame_is_key = false;
    return;
  }
  track.waiting_for_keyframe = false;
  const int64_t pts_us = track.have_rtp_timestamp
                             ? static_cast<int64_t>((static_cast<uint64_t>(packet.Timestamp() - track.first_rtp_timestamp) * 1'000'000U) / track.clock_rate)
                             : 0;
  if (!track.have_rtp_timestamp) {
    track.have_rtp_timestamp = true;
    track.first_rtp_timestamp = packet.Timestamp();
  }
  auto frame = media::MediaFrame::CreateShared(track.frame_data.size(), media::MediaType::VIDEO);
  std::memcpy(frame->data(), track.frame_data.data(), track.frame_data.size());
  frame->SetPts(base::Timestamp::Micros(pts_us));
  frame->SetDts(base::Timestamp::Micros(pts_us));
  frame->SetPictureType(key_frame ? media::PictureType::I : media::PictureType::P);
  track.packet_source->QueueAccessunit(std::move(frame));
  track.last_pts_us = pts_us;
  track.frame_data.clear();
  track.frame_is_key = false;
}

void RTSPSource::QueueAac(Track& track,
                          const media::rtp_rtcp::RtpPacketReceived& packet) {
  const auto payload = packet.payload();
  if (payload.size() < 2U) {
    return;
  }
  const uint32_t au_header_bits =
      (static_cast<uint32_t>(payload[0]) << 8U) | payload[1];
  if (au_header_bits < 16U || au_header_bits % 16U != 0U ||
      payload.size() < 2U + au_header_bits / 8U) {
    return;
  }
  const uint32_t au_count = au_header_bits / 16U;
  size_t header_offset = 2U;
  size_t data_offset = 2U + au_header_bits / 8U;
  if (!track.have_rtp_timestamp) {
    track.have_rtp_timestamp = true;
    track.first_rtp_timestamp = packet.Timestamp();
  }
  for (uint32_t index = 0; index < au_count; ++index) {
    const uint32_t au_size = (static_cast<uint32_t>(payload[header_offset]) << 5U) |
                             (payload[header_offset + 1U] >> 3U);
    header_offset += 2U;
    if (data_offset + au_size > payload.size()) {
      return;
    }
    const int64_t pts_us = static_cast<int64_t>(
        (static_cast<uint64_t>(packet.Timestamp() - track.first_rtp_timestamp +
                               index * 1024U) * 1'000'000U) /
        track.clock_rate);
    auto frame = media::MediaFrame::CreateShared(au_size, media::MediaType::AUDIO);
    std::memcpy(frame->data(), payload.data() + data_offset, au_size);
    frame->SetPts(base::Timestamp::Micros(pts_us));
    frame->SetDts(base::Timestamp::Micros(pts_us));
    track.packet_source->QueueAccessunit(std::move(frame));
    track.last_pts_us = pts_us;
    data_offset += au_size;
  }
}

void RTSPSource::CheckBuffering() {
  ++maintenance_ticks_;
  int64_t minimum_buffered_us = INT64_MAX;
  for (const auto& track : tracks_) {
    status_t result = OK;
    if (!track.packet_source || !track.packet_source->HasBufferAvailable(&result) ||
        (track.type == media::MediaType::VIDEO && track.waiting_for_keyframe)) {
      minimum_buffered_us = 0;
      break;
    }
    minimum_buffered_us =
        std::min(minimum_buffered_us, track.packet_source->GetBufferedDurationUs());
  }
  if (!prepared_ && minimum_buffered_us >= kInitialBufferUs) {
    prepared_ = true;
    NotifyPrepared(OK);
  }
  if (started_ && buffering_ && minimum_buffered_us >= kResumeBufferUs) {
    buffering_ = false;
    dequeue_ready_.store(true, std::memory_order_release);
    if (notify_) {
      notify_->OnBufferingEnd();
    }
  } else if (started_ && !buffering_ && minimum_buffered_us < kUnderflowBufferUs) {
    buffering_ = true;
    dequeue_ready_.store(false, std::memory_order_release);
    if (notify_) {
      notify_->OnBufferingStart();
    }
  }
  if (state_ == State::kPlaying &&
      maintenance_ticks_ >= kKeepAlivePeriodUs / kMaintenancePeriodUs) {
    maintenance_ticks_ = 0;
    SendRequest("OPTIONS", base_uri_);
  }
  if (transport_ == Transport::kUdp && state_ == State::kPlaying &&
      ++udp_no_packet_ticks_ >= kUdpFallbackUs / kMaintenancePeriodUs) {
    // A server that accepts UDP SETUP but never sends packets is common behind
    // NAT. Reconnect using interleaved transport rather than hanging forever.
    transport_ = Transport::kTcpInterleaved;
    state_ = State::kConnecting;
    CloseOnNetworkThread(false);
    ConnectOnNetworkThread(generation_);
  }
}

void RTSPSource::NotifyPrepared(status_t result) {
  if (!notify_) {
    return;
  }
  if (result == OK) {
    notify_->OnFlagsChanged(FLAG_CAN_PAUSE | FLAG_DYNAMIC_DURATION);
    if (auto* track = FindTrackByTypeLocked(media::MediaType::VIDEO); track && track->format) {
      auto format = track->format;
      notify_->OnVideoSizeChanged(format);
    }
  }
  notify_->OnPrepared(result);
}

void RTSPSource::NotifyError(status_t result) {
  state_ = State::kError;
  if (!prepared_) {
    NotifyPrepared(result);
  } else if (notify_) {
    notify_->OnError(result);
  }
}

void RTSPSource::OnSocketClosed(base::net::AsyncPacketSocket* socket,
                                int32_t /* error */) {
  std::scoped_lock lock(lock_);
  if (socket == rtsp_socket_.get() && state_ != State::kStopping &&
      state_ != State::kIdle) {
    NotifyError(DEAD_OBJECT);
  }
}

void RTSPSource::CloseOnNetworkThread(bool send_teardown) {
  if (send_teardown && rtsp_socket_ && !session_id_.empty()) {
    SendRequest("TEARDOWN", base_uri_);
  }
  for (auto& track : tracks_) {
    if (track.rtp_socket) {
      track.rtp_socket->Close();
    }
    if (track.rtcp_socket) {
      track.rtcp_socket->Close();
    }
    track.rtp_socket.reset();
    track.rtcp_socket.reset();
  }
  if (rtsp_socket_) {
    rtsp_socket_->Close();
    rtsp_socket_.reset();
  }
  session_id_.clear();
  read_buffer_.clear();
}

bool RTSPSource::ParseUrlLocked() {
  const std::string lower = ToLower(url_);
  if (!lower.starts_with("rtsp://")) {
    return false;
  }
  const size_t authority_begin = 7U;
  const size_t path_begin = url_.find('/', authority_begin);
  const std::string authority = url_.substr(authority_begin, path_begin - authority_begin);
  const size_t at = authority.rfind('@');
  const std::string host_port = at == std::string::npos ? authority : authority.substr(at + 1U);
  if (at != std::string::npos) {
    const std::string user_info = authority.substr(0, at);
    const size_t colon = user_info.find(':');
    username_ = user_info.substr(0, colon);
    password_ = colon == std::string::npos ? "" : user_info.substr(colon + 1U);
  }
  const size_t colon = host_port.rfind(':');
  host_ = colon == std::string::npos ? host_port : host_port.substr(0, colon);
  if (host_.empty() || host_.find('[') != std::string::npos) {
    return false;
  }
  if (colon != std::string::npos) {
    uint32_t parsed_port = 0;
    const auto [ptr, error] = std::from_chars(host_port.data() + colon + 1U,
                                              host_port.data() + host_port.size(),
                                              parsed_port);
    if (error != std::errc{} || ptr != host_port.data() + host_port.size() ||
        parsed_port == 0 || parsed_port > UINT16_MAX) {
      return false;
    }
    port_ = static_cast<uint16_t>(parsed_port);
  }
  // RTSP request URIs must not contain userinfo. Keep credentials only for
  // Authorization generation.
  base_uri_ = "rtsp://" + host_ + ":" + std::to_string(port_);
  if (path_begin != std::string::npos) {
    base_uri_ += url_.substr(path_begin);
  }
  return true;
}

bool RTSPSource::BuildAuthenticationLocked(const std::string& challenge) {
  if (!authentication_header_.empty() || username_.empty()) {
    return false;
  }
  const std::string lower = ToLower(challenge);
  if (lower.starts_with("basic")) {
    authentication_header_ = "Basic " + Base64Encode(username_ + ":" + password_);
    return true;
  }
  if (!lower.starts_with("digest")) {
    return false;
  }
  const auto parameters = ParseAuthParameters(Trim(challenge.substr(6U)));
  const auto realm = parameters.find("realm");
  const auto nonce = parameters.find("nonce");
  if (realm == parameters.end() || nonce == parameters.end()) {
    return false;
  }
  const std::string qop = parameters.contains("qop") ? ToLower(parameters.at("qop")) : "";
  if (!qop.empty() && qop.find("auth") == std::string::npos) {
    return false;
  }
  const std::string uri = pending_uri_;
  const std::string ha1 = Md5Hex(username_ + ":" + realm->second + ":" + password_);
  const std::string ha2 = Md5Hex(pending_method_ + ":" + uri);
  std::ostringstream authorization;
  authorization << "Digest username=\"" << username_ << "\", realm=\""
                << realm->second << "\", nonce=\"" << nonce->second
                << "\", uri=\"" << uri << "\", algorithm=MD5";
  if (qop.empty()) {
    authorization << ", response=\"" << Md5Hex(ha1 + ":" + nonce->second + ":" + ha2)
                  << "\"";
  } else {
    const std::string nonce_count = "00000001";
    const std::string cnonce = Md5Hex(std::to_string(++digest_nonce_count_)).substr(0, 16U);
    const std::string response = Md5Hex(ha1 + ":" + nonce->second + ":" + nonce_count +
                                        ":" + cnonce + ":auth:" + ha2);
    authorization << ", response=\"" << response << "\", qop=auth, nc="
                  << nonce_count << ", cnonce=\"" << cnonce << "\"";
  }
  authentication_header_ = authorization.str();
  return true;
}

bool RTSPSource::ParseSdp(const std::string& sdp) {
  tracks_.clear();
  Track* current = nullptr;
  std::istringstream lines(sdp);
  for (std::string line; std::getline(lines, line);) {
    line = Trim(std::move(line));
    if (line.starts_with("m=")) {
      std::istringstream stream(line.substr(2U));
      std::string media_name;
      std::string port;
      std::string protocol;
      uint32_t payload_type = 0;
      stream >> media_name >> port >> protocol >> payload_type;
      if ((ToLower(media_name) == "video" || ToLower(media_name) == "audio") &&
          payload_type <= UINT8_MAX) {
        tracks_.emplace_back();
        current = &tracks_.back();
        current->type = ToLower(media_name) == "video" ? media::MediaType::VIDEO
                                                         : media::MediaType::AUDIO;
        current->payload_type = static_cast<uint8_t>(payload_type);
      } else {
        current = nullptr;
      }
      continue;
    }
    if (!current || !line.starts_with("a=")) {
      continue;
    }
    const std::string attribute = line.substr(2U);
    if (attribute.starts_with("rtpmap:")) {
      const size_t space = attribute.find(' ');
      if (space == std::string::npos) {
        continue;
      }
      std::istringstream value(attribute.substr(space + 1U));
      std::string clock;
      std::getline(value, current->encoding, '/');
      std::getline(value, clock, '/');
      std::from_chars(clock.data(), clock.data() + clock.size(), current->clock_rate);
      std::string channels;
      if (std::getline(value, channels, '/')) {
        std::from_chars(channels.data(), channels.data() + channels.size(), current->channels);
      }
    } else if (attribute.starts_with("control:")) {
      const std::string control = attribute.substr(8U);
      current->control_uri = control.starts_with("rtsp://") ? control :
          (base_uri_.ends_with("/") ? base_uri_ + control : base_uri_ + "/" + control);
    } else if (attribute.starts_with("fmtp:")) {
      const size_t space = attribute.find(' ');
      if (space == std::string::npos) {
        continue;
      }
      std::istringstream parameters(attribute.substr(space + 1U));
      for (std::string parameter; std::getline(parameters, parameter, ';');) {
        const size_t equal = parameter.find('=');
        if (equal != std::string::npos) {
          current->fmtp.emplace(ToLower(Trim(parameter.substr(0, equal))),
                                Trim(parameter.substr(equal + 1U)));
        }
      }
    }
  }
  std::erase_if(tracks_, [this](Track& track) { return !ConfigureTrackFromSdp(track); });
  for (auto& track : tracks_) {
    if (track.type == media::MediaType::AUDIO) {
      audio_packet_source_ = track.packet_source;
    } else {
      video_packet_source_ = track.packet_source;
      source_format_ = track.format;
    }
  }
  return !tracks_.empty();
}

bool RTSPSource::ConfigureTrackFromSdp(Track& track) {
  const std::string encoding = ToLower(track.encoding);
  if (track.control_uri.empty()) {
    return false;
  }
  if (track.type == media::MediaType::VIDEO && encoding == "h264") {
    track.format = media::MediaMeta::CreatePtr(media::MediaType::VIDEO,
                                                 media::MediaMeta::FormatType::kTrack);
    track.format->SetMime(media::MEDIA_MIMETYPE_VIDEO_AVC).SetCodec(media::CodecId::AVE_CODEC_ID_H264);
    track.depacketizer =
        std::make_unique<media::rtp_rtcp::VideoRtpDepacketizerH264>();
    if (const auto it = track.fmtp.find("sprop-parameter-sets");
        it != track.fmtp.end()) {
      std::vector<std::string> values;
      std::istringstream stream(it->second);
      for (std::string value; std::getline(stream, value, ',');) values.push_back(Trim(value));
      const auto config = MakeAnnexBConfig(values);
      if (!config.empty()) {
        track.format->SetPrivateData(static_cast<uint32_t>(config.size()), config.data());
      }
    }
  } else if (track.type == media::MediaType::VIDEO && (encoding == "h265" || encoding == "hevc")) {
    track.format = media::MediaMeta::CreatePtr(media::MediaType::VIDEO,
                                                 media::MediaMeta::FormatType::kTrack);
    track.format->SetMime(media::MEDIA_MIMETYPE_VIDEO_HEVC).SetCodec(media::CodecId::AVE_CODEC_ID_HEVC);
    track.depacketizer =
        std::make_unique<media::rtp_rtcp::VideoRtpDepacketizerH265>();
    std::vector<std::string> values;
    for (const char* key : {"sprop-vps", "sprop-sps", "sprop-pps"}) {
      if (const auto it = track.fmtp.find(key); it != track.fmtp.end()) values.push_back(it->second);
    }
    const auto config = MakeAnnexBConfig(values);
    if (!config.empty()) {
      track.format->SetPrivateData(static_cast<uint32_t>(config.size()), config.data());
    }
  } else if (track.type == media::MediaType::AUDIO && encoding == "mpeg4-generic" &&
             track.clock_rate > 0U && track.channels > 0U) {
    const auto config = track.fmtp.find("config");
    if (config == track.fmtp.end()) {
      return false;
    }
    const auto private_data = HexToBytes(config->second);
    if (private_data.empty()) {
      return false;
    }
    track.format = media::MediaMeta::CreatePtr(media::MediaType::AUDIO,
                                                 media::MediaMeta::FormatType::kTrack);
    track.format->SetMime(media::MEDIA_MIMETYPE_AUDIO_AAC)
        .SetCodec(media::CodecId::AVE_CODEC_ID_AAC)
        .SetSampleRate(track.clock_rate)
        .SetChannelLayout(ChannelLayoutForCount(track.channels))
        .SetPrivateData(static_cast<uint32_t>(private_data.size()), private_data.data());
  } else {
    return false;
  }
  track.packet_source = std::make_shared<PacketSource>(track.format);
  return track.depacketizer != nullptr || track.type == media::MediaType::AUDIO;
}

void RTSPSource::ResetMediaLocked() {
  for (auto& track : tracks_) {
    if (track.packet_source) {
      track.packet_source->Clear();
    }
    track.frame_data.clear();
    track.frame_is_key = false;
    track.waiting_for_keyframe = true;
    track.have_rtp_timestamp = false;
  }
}

RTSPSource::Track* RTSPSource::FindTrackBySocket(base::net::AsyncPacketSocket* socket) {
  for (auto& track : tracks_) {
    if (track.rtp_socket.get() == socket || track.rtcp_socket.get() == socket) {
      return &track;
    }
  }
  return nullptr;
}

RTSPSource::Track* RTSPSource::FindTrackByChannel(uint8_t channel) {
  for (auto& track : tracks_) {
    if (track.rtp_channel == channel || track.rtcp_channel == channel) {
      return &track;
    }
  }
  return nullptr;
}

RTSPSource::Track* RTSPSource::FindTrackByTypeLocked(media::MediaType type) {
  for (auto& track : tracks_) {
    if (track.type == type) {
      return &track;
    }
  }
  return nullptr;
}

}  // namespace ave::player
