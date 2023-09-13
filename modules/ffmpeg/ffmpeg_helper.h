/*
 * ffmpeg_helper.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef FFMPEG_HELPER_H
#define FFMPEG_HELPER_H

#include <memory>

#include "common/buffer.h"
#include "common/media_defs.h"
#include "common/meta_data.h"
#include "libavutil/rational.h"

extern "C" {
#include "third_party/ffmpeg/libavcodec/avcodec.h"
#include "third_party/ffmpeg/libavformat/avformat.h"
#include "third_party/ffmpeg/libavformat/avio.h"
}

namespace avp {

// Converts an int64 timestamp in |time_base| units to a base::TimeDelta.
// For example if |timestamp| equals 11025 and |time_base| equals {1, 44100}
// then the return value will be a base::TimeDelta for 0.25 seconds since that
// is how much time 11025/44100ths of a second represents.
int64_t ConvertFromTimeBase(const AVRational& time_base, int64_t pkt_pts);

// Converts a base::TimeDelta into an int64 timestamp in |time_base| units.
// For example if |timestamp| is 0.5 seconds and |time_base| is {1, 44100}, then
// the return value will be 22050 since that is how many 1/44100ths of a second
// represent 0.5 seconds.
int64_t ConvertToTimeBase(const AVRational& time_base, const int64_t time_us);

void ffmpeg_log_default(void* p_unused,
                        int i_level,
                        const char* psz_fmt,
                        va_list arg);
const char* VideoCodecId2Mime(AVCodecID codecId);
const char* AudioCodecId2Mime(AVCodecID codecId);
AVCodecID AvpCodecToCodecID(CodecType codecType);

void AVStreamToAudioMeta(const AVStream* audioStream,
                         std::shared_ptr<MetaData>& meta);
void AVStreamToVideoMeta(const AVStream* videoStream,
                         std::shared_ptr<MetaData>& meta);
void VideoFormatToAVCodecContext(const std::shared_ptr<Message>& format,
                                 AVCodecContext* codecContext);
void AudioFormatToAVCodecContext(const std::shared_ptr<Message>& format,
                                 AVCodecContext* codecContext);

std::shared_ptr<Buffer> createBufferFromAvPacket(const AVPacket* pkt,
                                                 const AVRational& time_base);
std::shared_ptr<Buffer> createAudioBufferFromAvFrame(
    const AVFrame* frame,
    const AVRational& time_base);
std::shared_ptr<Buffer> createVideoBufferFromAvFrame(
    const AVFrame* frame,
    const AVRational& time_base);

} /* namespace avp */

#endif /* !FFMPEG_HELPER_H */
