/*
 *  Copyright (C) 2025 youfa <vsyfar@gmail.com>
 *
 *  Distributed under terms of the GPLv2 license.
 */

package io.github.yoofa.avp;

import io.github.yoofa.media.MediaFormat;

import org.jni_zero.CalledByNative;
import org.jni_zero.JniType;

/**
 * Describes a media track (audio, video, or subtitle) in the current media.
 * Mirrors native MediaMeta track information.
 */
public class TrackInfo {

    /** Track type constants. */
    public static final int MEDIA_TRACK_TYPE_UNKNOWN = -1;
    public static final int MEDIA_TRACK_TYPE_VIDEO = 0;
    public static final int MEDIA_TRACK_TYPE_AUDIO = 1;
    public static final int MEDIA_TRACK_TYPE_SUBTITLE = 2;
    public static final int MEDIA_TRACK_TYPE_TIMED_TEXT = 3;

    private final int trackType;
    private final int trackIndex;
    private String mimeType;
    private String language;
    private MediaFormat format;

    @CalledByNative
    public TrackInfo(int trackType, int trackIndex) {
        this.trackType = trackType;
        this.trackIndex = trackIndex;
    }

    /**
     * @return The track type (MEDIA_TRACK_TYPE_VIDEO, MEDIA_TRACK_TYPE_AUDIO, etc.).
     */
    public int getTrackType() {
        return trackType;
    }

    /**
     * @return The track index within the media.
     */
    public int getTrackIndex() {
        return trackIndex;
    }

    /**
     * @return The MIME type of the track (e.g., "video/avc", "audio/mp4a-latm").
     */
    public String getMimeType() {
        return mimeType;
    }

    @CalledByNative
    public void setMimeType(@JniType("std::string") String mimeType) {
        this.mimeType = mimeType;
    }

    /**
     * @return The language code of the track (e.g., "en", "zh"), or null.
     */
    public String getLanguage() {
        return language;
    }

    /**
     * Sets the language code for this track.
     * @param language The ISO 639-1 language code (e.g., "en", "zh").
     */
    public void setLanguage(String language) {
        this.language = language;
    }

    /**
     * @return The detailed media format for this track, or null.
     */
    public MediaFormat getFormat() {
        return format;
    }

    /**
     * Sets the detailed media format for this track.
     * @param format The media format description.
     */
    public void setFormat(MediaFormat format) {
        this.format = format;
    }

    @Override
    public String toString() {
        String typeName;
        switch (trackType) {
            case MEDIA_TRACK_TYPE_VIDEO:
                typeName = "Video";
                break;
            case MEDIA_TRACK_TYPE_AUDIO:
                typeName = "Audio";
                break;
            case MEDIA_TRACK_TYPE_SUBTITLE:
                typeName = "Subtitle";
                break;
            case MEDIA_TRACK_TYPE_TIMED_TEXT:
                typeName = "TimedText";
                break;
            default:
                typeName = "Unknown";
                break;
        }
        return "TrackInfo{" + typeName + ", index=" + trackIndex
                + ", mime=" + mimeType + ", lang=" + language + "}";
    }
}
