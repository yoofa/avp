/*
 * utils.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef UTILS_H
#define UTILS_H

#include <memory>

#include "base/errors.h"
#include "common/message.h"
#include "common/meta_data.h"

namespace avp {
status_t convertMetaDataToMessage(const MetaData* meta,
                                  std::shared_ptr<Message>& format);

status_t convertMetaDataToMessage(const std::shared_ptr<MetaData>& meta,
                                  std::shared_ptr<Message>& format);

status_t convertMessageToMetaData(const std::shared_ptr<Message>& format,
                                  std::shared_ptr<MetaData>& meta);

// Returns a pointer to the next NAL start code in buffer of size |length|
// starting at |data|, or a pointer to the end of the buffer if the start code
// is not found.
// TODO: combine this with avc_utils::getNextNALUnit
const uint8_t* findNextNalStartCode(const uint8_t* data, size_t length);

struct HLSTime {
  int32_t mSeq;
  int64_t mTimeUs;
  std::shared_ptr<Message> mMeta;

  explicit HLSTime(const std::shared_ptr<Message>& meta = NULL);
  int64_t getSegmentTimeUs() const;
};

bool operator<(const HLSTime& t0, const HLSTime& t1);

} /* namespace avp */

#endif /* !UTILS_H */
