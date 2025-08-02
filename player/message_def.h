/*
 * message_def.h
 * Copyright (C) 2025 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AVE_AVP_MESSAGE_DEF_H_H_
#define AVE_AVP_MESSAGE_DEF_H_H_

namespace ave {
namespace player {

static const char* kError = "error";
static const char* kContentSource = "content_source";
static const char* kVideoRender = "video_render";
static const char* kFlags = "flags";
static const char* kConfigFormat = "config_format";
static const char* kHasVideo = "has-video";
static const char* kMediaFormat = "media_format";
static const char* kPercent = "percent";
static const char* kMediaType = "media_type";
static const char* kWhat = "what";
static const char* kSynchronizer = "synchronizer";
static const char* kBuffer = "buffer";
static const char* kIndex = "index";
static const char* kParameters = "parameters";
static const char* kSeekToUs = "seek_to_us";
static const char* kSeekMode = "seek_mode";
static const char* kGeneration = "generation";
}  // namespace player
}  // namespace ave

#endif /* !AVE_AVP_MESSAGE_DEF_H_H_ */
