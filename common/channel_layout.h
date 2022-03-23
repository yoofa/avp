/*
 * channel_layout.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef CHANNEL_LAYOUT_H
#define CHANNEL_LAYOUT_H

enum ChannelLayout {
  CHANNEL_LAYOUT_NONE = 0,
  CHANNEL_LAYOUT_UNSUPPORTED,

  // Front C
  CHANNEL_LAYOUT_MONO,

  // Front L, Front R
  CHANNEL_LAYOUT_STEREO,

  // Front L, Front R, Back C
  CHANNEL_LAYOUT_2_1,

  // Front L, Front R, Front C
  CHANNEL_LAYOUT_SURROUND,

  // Front L, Front R, Front C, Back C
  CHANNEL_LAYOUT_4_0,

  // Front L, Front R, Side L, Side R
  CHANNEL_LAYOUT_2_2,

  // Front L, Front R, Back L, Back R
  CHANNEL_LAYOUT_QUAD,

  // Front L, Front R, Front C, Side L, Side R
  CHANNEL_LAYOUT_5_0,

  // Front L, Front R, Front C, Side L, Side R, LFE
  CHANNEL_LAYOUT_5_1,

  // Front L, Front R, Front C, Back L, Back R
  CHANNEL_LAYOUT_5_0_BACK,

  // Front L, Front R, Front C, Back L, Back R, LFE
  CHANNEL_LAYOUT_5_1_BACK,

  // Front L, Front R, Front C, Side L, Side R, Back L, Back R
  CHANNEL_LAYOUT_7_0,

  // Front L, Front R, Front C, Side L, Side R, LFE, Back L, Back R
  CHANNEL_LAYOUT_7_1,

  // Front L, Front R, Front C, Back L, Back R, LFE, Front LofC, Front RofC
  CHANNEL_LAYOUT_7_1_WIDE,

  // Stereo L, Stereo R
  CHANNEL_LAYOUT_STEREO_DOWNMIX,

  // Total number of layouts.
  CHANNEL_LAYOUT_MAX
};

enum Channels {
  LEFT = 0,
  RIGHT,
  CENTER,
  LFE,
  BACK_LEFT,
  BACK_RIGHT,
  LEFT_OF_CENTER,
  RIGHT_OF_CENTER,
  BACK_CENTER,
  SIDE_LEFT,
  SIDE_RIGHT,
  STEREO_LEFT,
  STEREO_RIGHT,
  CHANNELS_MAX
};

extern const int kChannelOrderings[CHANNEL_LAYOUT_MAX][CHANNELS_MAX];

int ChannelLayoutToChannelCount(ChannelLayout layout);

#endif /* !CHANNEL_LAYOUT_H */
