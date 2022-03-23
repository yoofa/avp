/*
 * meta_data_utils.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef META_DATA_UTILS_H
#define META_DATA_UTILS_H

#include "common/meta_data.h"

namespace avp {

struct ABuffer;
bool MakeAVCCodecSpecificData(MetaData& meta, const uint8_t* data, size_t size);
bool MakeAVCCodecSpecificData(std::shared_ptr<Message>& meta,
                              const uint8_t* data,
                              size_t size);

bool MakeAACCodecSpecificData(MetaData& meta, const uint8_t* data, size_t size);
bool MakeAACCodecSpecificData(MetaData& meta,
                              unsigned profile,
                              unsigned sampling_freq_index,
                              unsigned channel_configuration);

bool MakeAACCodecSpecificData(std::shared_ptr<Message>& meta,
                              unsigned profile,
                              unsigned sampling_freq_index,
                              unsigned channel_configuration);

// void parseVorbisComment(Message* fileMeta,
//                        const char* comment,
//                        size_t commentLength);
} /* namespace avp */

#endif /* !META_DATA_UTILS_H */
