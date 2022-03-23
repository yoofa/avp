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

extern "C" {
#include "third_party/ffmpeg/libavcodec/avcodec.h"
#include "third_party/ffmpeg/libavformat/avformat.h"
#include "third_party/ffmpeg/libavformat/avio.h"
}

namespace avp {

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

std::shared_ptr<Buffer> createBufferFromAvPacket(AVPacket* pkt);
std::shared_ptr<Buffer> createAudioBufferFromAvFrame(AVFrame* frame);
std::shared_ptr<Buffer> createVideoBufferFromAvFrame(AVFrame* frame);

} /* namespace avp */

#endif /* !FFMPEG_HELPER_H */
