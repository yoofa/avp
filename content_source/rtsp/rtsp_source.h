/*
 * rtsp_source.h
 * Copyright (C) 2026 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AVP_CONTENT_SOURCE_RTSP_RTSP_SOURCE_H_
#define AVP_CONTENT_SOURCE_RTSP_RTSP_SOURCE_H_

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "api/content_source/content_source.h"
#include "base/net/async_packet_socket.h"
#include "base/net/network_thread.h"
#include "base/task_util/repeating_task.h"
#include "base/task_util/task_runner.h"
#include "core/packet_source.h"
#include "media/modules/rtp_rtcp/src/video/video_rtp_depacketizer.h"
#include "media/modules/rtp_rtcp/src/rtp/rtp_packet_received.h"

namespace ave::player {

// RTSP live source. All protocol and socket callbacks run on network_thread_.
// PacketSource is the only object shared with decoder threads.
class RTSPSource final : public ContentSource,
                         public sigslot::has_slots<>,
                         public std::enable_shared_from_this<RTSPSource> {
 public:
  RTSPSource();
  ~RTSPSource() override;

  status_t SetDataSource(
      const char* url,
      const std::unordered_map<std::string, std::string>& headers);

  void SetNotify(Notify* notify) override;
  void Prepare() override;
  void Start() override;
  void Stop() override;
  void Pause() override;
  void Resume() override;
  status_t DequeueAccessUnit(
      media::MediaType track_type,
      std::shared_ptr<media::MediaFrame>& access_unit) override;
  std::shared_ptr<media::MediaMeta> GetFormat() override;
  status_t GetDuration(int64_t* duration_us) override;
  size_t GetTrackCount() const override;
  std::shared_ptr<media::MediaMeta> GetTrackInfo(
      size_t track_index) const override;
  std::shared_ptr<media::MediaMeta> GetTrackInfo(
      media::MediaType track_type) const override;
  status_t SelectTrack(size_t track_index, bool select) override;
  bool IsStreaming() const override;

 private:
  enum class Transport { kUdp, kTcpInterleaved };
  enum class State { kIdle, kConnecting, kOptions, kDescribe, kSetup, kPlay,
                     kPlaying, kPaused, kStopping, kError };

  struct Track;

  void PrepareOnNetworkThread(uint64_t generation);
  void ConnectOnNetworkThread(uint64_t generation);
  void CloseOnNetworkThread(bool send_teardown);
  void SendRequest(const std::string& method,
                   const std::string& uri,
                   const std::vector<std::string>& extra_headers = {});
  void HandleRtspResponse(const std::string& response);
  void HandleInterleavedData(uint8_t channel, const uint8_t* data, size_t size);
  void HandleRtpPacket(Track& track, const uint8_t* data, size_t size);
  void OnTcpRead(base::net::AsyncPacketSocket* socket,
                 const uint8_t* data,
                 size_t size,
                 const base::net::SocketAddress& remote_addr,
                 int64_t timestamp_us);
  void OnUdpRead(base::net::AsyncPacketSocket* socket,
                 const uint8_t* data,
                 size_t size,
                 const base::net::SocketAddress& remote_addr,
                 int64_t timestamp_us);
  void OnSocketClosed(base::net::AsyncPacketSocket* socket, int32_t error);
  void OnSocketConnected(base::net::AsyncPacketSocket* socket);

  bool ParseUrlLocked();
  bool BuildAuthenticationLocked(const std::string& challenge);
  bool ParseSdp(const std::string& sdp);
  bool ConfigureTrackFromSdp(Track& track);
  void ContinueSetup();
  void StartPlayback();
  void NotifyPrepared(status_t result);
  void NotifyError(status_t result);
  void CheckBuffering();
  void QueueVideo(Track& track,
                  const media::rtp_rtcp::RtpPacketReceived& packet);
  void QueueAac(Track& track,
                const media::rtp_rtcp::RtpPacketReceived& packet);
  void ResetMediaLocked();
  Track* FindTrackBySocket(base::net::AsyncPacketSocket* socket);
  Track* FindTrackByChannel(uint8_t channel);
  Track* FindTrackByTypeLocked(media::MediaType type);

  mutable std::mutex lock_;
  Notify* notify_ = nullptr;
  std::string url_;
  std::unordered_map<std::string, std::string> headers_;
  std::string host_;
  std::string base_uri_;
  std::string username_;
  std::string password_;
  uint16_t port_ = 554;
  std::string session_id_;
  std::string authentication_header_;
  std::string pending_method_;
  std::string pending_uri_;
  std::vector<std::string> pending_extra_headers_;
  uint32_t digest_nonce_count_ = 0;
  std::string read_buffer_;
  uint32_t cseq_ = 1;
  uint32_t maintenance_ticks_ = 0;
  uint32_t udp_no_packet_ticks_ = 0;
  size_t setup_track_index_ = 0;
  State state_ = State::kIdle;
  Transport transport_ = Transport::kUdp;
  bool prepared_ = false;
  bool started_ = false;
  bool buffering_ = true;
  uint64_t generation_ = 0;
  std::vector<Track> tracks_;
  std::shared_ptr<media::MediaMeta> source_format_;
  std::shared_ptr<PacketSource> audio_packet_source_;
  std::shared_ptr<PacketSource> video_packet_source_;
  std::atomic_bool dequeue_ready_ = false;

  base::net::NetworkThread network_thread_;
  std::unique_ptr<base::net::AsyncPacketSocket> rtsp_socket_;
  std::unique_ptr<base::TaskRunner> timer_task_runner_;
  base::RepeatingTaskHandle maintenance_task_;
};

}  // namespace ave::player

#endif  // AVP_CONTENT_SOURCE_RTSP_RTSP_SOURCE_H_
