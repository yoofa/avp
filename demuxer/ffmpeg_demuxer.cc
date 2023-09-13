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
#include "common/utils.h"
#include "player/data_source.h"
#include "player/media_source.h"

#include "modules/ffmpeg/ffmpeg_helper.h"

namespace avp {

enum { kBufferSize = 32 * 1024 };
int lastVideoTimeUs = 0;

static int AVIOReadOperation(void* opaque, uint8_t* buf, int size) {
  DataSource* dataSource = reinterpret_cast<DataSource*>(opaque);
  ssize_t result = dataSource->read(buf, size);
  if (result < 0)
    result = AVERROR(EIO);
  return static_cast<int>(result);
}

static int64_t AVIOSeekOperation(void* opaque, int64_t offset, int whence) {
  DataSource* dataSource = reinterpret_cast<DataSource*>(opaque);
  int64_t new_offset = AVERROR(EIO);
  switch (whence) {
    case SEEK_SET:
    case SEEK_CUR:
    case SEEK_END:
      new_offset = dataSource->seek(offset, whence);
      break;

    case AVSEEK_SIZE:
      dataSource->getSize(&new_offset);
      break;

    default:
      break;
  }
  if (new_offset < 0)
    new_offset = AVERROR(EIO);
  return new_offset;
}

////////////////////////////////////

struct FFmpegSource : public MediaSource {
  FFmpegSource(FFmpegDemuxer* demuxer,
               size_t index,
               std::shared_ptr<MetaData> meta);
  virtual ~FFmpegSource() override;

  virtual status_t start() override;
  virtual status_t stop() override;
  virtual status_t read(std::shared_ptr<Buffer>& buffer,
                        const ReadOptions* options) override;
  virtual std::shared_ptr<MetaData> getMeta() override;

 private:
  FFmpegDemuxer* mDemuxer;
  size_t mTrackIndex;
  std::shared_ptr<MetaData> mMeta;
};

FFmpegSource::FFmpegSource(FFmpegDemuxer* demuxer,
                           size_t index,
                           std::shared_ptr<MetaData> meta)
    : mDemuxer(demuxer), mTrackIndex(index), mMeta(std::move(meta)) {}

FFmpegSource::~FFmpegSource() {}

status_t FFmpegSource::start() {
  return OK;
}

status_t FFmpegSource::stop() {
  return OK;
}

status_t FFmpegSource::read(std::shared_ptr<Buffer>& buffer,
                            const ReadOptions* options) {
  return mDemuxer->readAvFrame(buffer, mTrackIndex, options);
}

std::shared_ptr<MetaData> FFmpegSource::getMeta() {
  LOG(LS_INFO) << "getMeta:" << mMeta->toString();
  return mMeta;
}

////////////////////////////////////
FFmpegDemuxer::TrackInfo::TrackInfo(size_t index,
                                    std::shared_ptr<MetaData> meta,
                                    std::shared_ptr<FFmpegSource> source)
    : mTrackIndex(index), mMeta(std::move(meta)), mSource(std::move(source)) {}

FFmpegDemuxer::TrackInfo::~TrackInfo() {}

size_t FFmpegDemuxer::TrackInfo::packetSize() {
  return mPackets.size();
}

status_t FFmpegDemuxer::TrackInfo::enqueuePacket(
    std::shared_ptr<Buffer> packet) {
  mPackets.push_back(std::move(packet));
  return OK;
}

status_t FFmpegDemuxer::TrackInfo::dequeuePacket(
    std::shared_ptr<Buffer>& packet) {
  if (mPackets.size() == 0) {
    return WOULD_BLOCK;
  }

  packet = mPackets.front();
  mPackets.pop_front();

  return OK;
}

////////////////////////////////////

FFmpegDemuxer::FFmpegDemuxer(std::shared_ptr<DataSource> dataSource)
    : mDataSource(dataSource) {
  av_log_set_level(AV_LOG_QUIET);
  mFormatContext = avformat_alloc_context();
  mIOContext = avio_alloc_context(
      static_cast<unsigned char*>(av_malloc(kBufferSize)), kBufferSize, 0,
      mDataSource.get(), &AVIOReadOperation, nullptr, &AVIOSeekOperation);
  mIOContext->seekable = mDataSource->flags() & DataSource::kSeekable;
  mIOContext->write_flag = 0;

  mFormatContext->flags |= AVFMT_FLAG_CUSTOM_IO;
  mFormatContext->pb = mIOContext;
}

FFmpegDemuxer::~FFmpegDemuxer() {}

status_t FFmpegDemuxer::init() {
  AVIOSeekOperation(mIOContext->opaque, 0, SEEK_CUR);
  int ret;
  ret = avformat_open_input(&mFormatContext, nullptr, nullptr, nullptr);
  ret = avformat_find_stream_info(mFormatContext, nullptr);
  mMeta = std::make_shared<MetaData>();

  LOG(LS_VERBOSE) << "init, streams: " << mFormatContext->nb_streams;

  mMeta->setInt64(kKeyDuration, mFormatContext->duration);
  mMeta->setInt64(kKeyBitRate, mFormatContext->bit_rate);
  LOG(LS_VERBOSE) << " demuxer meta <" << mMeta->toString() << ">";

  for (size_t i = 0; i < mFormatContext->nb_streams; i++) {
    const AVStream* avStream = mFormatContext->streams[i];
    if (avStream != nullptr) {
      addTrack(avStream, i);
    }
  }

  return OK;
}

std::shared_ptr<MetaData> createMetaFromAVStream(const AVStream* avStream) {
  std::shared_ptr<MetaData> meta = std::make_shared<MetaData>();
  // AVCodecParameters codecPar = avStream->codecpar;
  //
  meta->setInt64(kKeyDuration, avStream->duration);

  switch (avStream->codecpar->codec_type) {
    case AVMEDIA_TYPE_VIDEO: {
      AVStreamToVideoMeta(avStream, meta);
      break;
    }
    case AVMEDIA_TYPE_AUDIO: {
      AVStreamToAudioMeta(avStream, meta);
      break;
    }
    case AVMEDIA_TYPE_SUBTITLE: {
      break;
    }
    default:
      return nullptr;
  }
  meta->setInt32(kKeyBitRate, avStream->codecpar->bit_rate);

  const char* mime;
  meta->findCString(kKeyMIMEType, &mime);

  LOG(LS_VERBOSE) << "avStream.meta: " << meta->toString();

  return meta;
}

status_t FFmpegDemuxer::addTrack(const AVStream* avStream, size_t index) {
  std::shared_ptr<MetaData> meta = createMetaFromAVStream(avStream);
  // if (avStream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
  LOG(LS_INFO) << "dump(" << avStream->codecpar->codec_type << "):";
  hexdump(avStream->codecpar->extradata, avStream->codecpar->extradata_size);
  //}

  std::shared_ptr<FFmpegSource> source(
      std::make_shared<FFmpegSource>(this, index, meta));
  mTracks.emplace_back(index, meta, source);

  return OK;
}

size_t FFmpegDemuxer::getTrackCount() {
  return mTracks.size();
}

std::shared_ptr<MediaSource> FFmpegDemuxer::getTrack(size_t trackIndex) {
  if (trackIndex >= mTracks.size()) {
    return nullptr;
  }
  return mTracks[trackIndex].mSource;
}

status_t FFmpegDemuxer::getDemuxerMeta(std::shared_ptr<MetaData>& metaData) {
  metaData = mMeta;
  return OK;
}

status_t FFmpegDemuxer::getTrackMeta(std::shared_ptr<MetaData>& metaData,
                                     size_t trackIndex) {
  if (trackIndex >= mTracks.size()) {
    return UNKNOWN_ERROR;
  }
  // TODO(youfa) use copy, not addRef
  metaData = mTracks[trackIndex].mMeta;
  return OK;
}

status_t FFmpegDemuxer::readAnAvPacket(size_t index) {
  AVPacket pkt;
  status_t err;

  while (true) {
    err = av_read_frame(mFormatContext, &pkt);
    if (err < 0) {
      return err;
    }

    DCHECK_GE(pkt.stream_index, 0);
    DCHECK_LT(static_cast<size_t>(pkt.stream_index), mTracks.size());

    if (pkt.stream_index == 0) {
      LOG(LS_INFO) << "readAnAvPacket, index:" << pkt.stream_index
                   << "time_base:"
                   << mFormatContext->streams[pkt.stream_index]->time_base.num
                   << "/"
                   << mFormatContext->streams[pkt.stream_index]->time_base.den
                   << ", pts:" << pkt.pts << ",time_us:"
                   << ConvertFromTimeBase(
                          mFormatContext->streams[pkt.stream_index]->time_base,
                          pkt.pts)
                   << ", diff:" << (pkt.pts - lastVideoTimeUs);

      lastVideoTimeUs = pkt.pts;
    }

    if (pkt.stream_index >= 0 &&
        pkt.stream_index < static_cast<int>(mTracks.size())) {
      mTracks[pkt.stream_index].enqueuePacket(createBufferFromAvPacket(
          &pkt, mFormatContext->streams[pkt.stream_index]->time_base));
    }
    if (static_cast<size_t>(pkt.stream_index) == index) {
      break;
    }
  }

  return OK;
}

status_t FFmpegDemuxer::readAvFrame(std::shared_ptr<Buffer>& buffer,
                                    size_t index,
                                    const MediaSource::ReadOptions* options) {
  if (index >= mTracks.size()) {
    return UNKNOWN_ERROR;
  }

  // TODO(youfa) readOption

  if (mTracks[index].packetSize() == 0) {
    return readAnAvPacket(index);
  }

  mTracks[index].dequeuePacket(buffer);

  return OK;
}

const char* FFmpegDemuxer::name() {
  return "FFmpeg-Demuxer";
}

} /* namespace avp */
