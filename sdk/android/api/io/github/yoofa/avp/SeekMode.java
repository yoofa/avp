/*
 *  Copyright (C) 2025 youfa <vsyfar@gmail.com>
 *
 *  Distributed under terms of the GPLv2 license.
 */

package io.github.yoofa.avp;

/**
 * Seek mode constants for {@link AvpPlayer#seekTo(int, SeekMode)}.
 * Mirrors native SeekMode enum from player_interface.h.
 */
public enum SeekMode {
    /**
     * Seek to the previous sync frame (keyframe) at or before the target.
     */
    SEEK_PREVIOUS_SYNC(0),

    /**
     * Seek to the next sync frame (keyframe) at or after the target.
     */
    SEEK_NEXT_SYNC(1),

    /**
     * Seek to the closest sync frame (keyframe) to the target.
     */
    SEEK_CLOSEST_SYNC(2),

    /**
     * Seek to the closest frame (may not be a keyframe).
     * This may be slower as it requires decoding from the nearest keyframe.
     */
    SEEK_CLOSEST(4);

    private final int nativeValue;

    SeekMode(int nativeValue) {
        this.nativeValue = nativeValue;
    }

    /**
     * @return The native integer value for this seek mode.
     */
    public int getNativeValue() {
        return nativeValue;
    }

    /**
     * Convert a native integer value to a SeekMode enum constant.
     *
     * @param value The native integer value.
     * @return The corresponding SeekMode, or SEEK_PREVIOUS_SYNC if not found.
     */
    public static SeekMode fromNativeValue(int value) {
        for (SeekMode mode : values()) {
            if (mode.nativeValue == value) {
                return mode;
            }
        }
        return SEEK_PREVIOUS_SYNC;
    }
}
