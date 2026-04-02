/*
 *  Copyright (C) 2025 youfa <vsyfar@gmail.com>
 *
 *  Distributed under terms of the GPLv2 license.
 */

package io.github.yoofa.avp;

import org.jni_zero.CalledByNative;

/**
 * Audio output sink interface for AVP player.
 *
 * <p>Implementations wrap a platform audio output (e.g., Android AudioTrack)
 * and are called from native code via JNI to write PCM data.
 *
 * <p>Users can supply a custom AudioSink via {@link AvpPlayer#setAudioSink}
 * or use the built-in {@link DefaultAudioSink}.
 */
public interface AudioSink {

    /** Audio encoding constants matching native audio_format_t PCM sub-types. */
    int ENCODING_PCM_16BIT = 1;
    int ENCODING_PCM_FLOAT = 2;
    int ENCODING_PCM_8BIT  = 3;
    int ENCODING_PCM_32BIT = 4;

    /**
     * Open the audio sink with the given configuration.
     *
     * @param sampleRate   Sample rate in Hz.
     * @param channelCount Number of audio channels.
     * @param encoding     One of the ENCODING_PCM_* constants.
     * @return true if opened successfully.
     */
    @CalledByNative
    boolean open(int sampleRate, int channelCount, int encoding);

    /**
     * Write PCM audio data to the sink.
     *
     * @param data   Audio data buffer.
     * @param offset Byte offset into the buffer.
     * @param size   Number of bytes to write.
     * @return Number of bytes actually written, or negative on error.
     */
    @CalledByNative
    int write(byte[] data, int offset, int size);

    /** Start playback. */
    @CalledByNative
    void start();

    /** Stop playback and reset position. */
    @CalledByNative
    void stop();

    /** Flush buffered data. */
    @CalledByNative
    void flush();

    /** Pause playback. */
    @CalledByNative
    void pause();

    /** Close the sink and release resources. */
    @CalledByNative
    void close();

    /** @return true if the sink is open and ready to accept data. */
    @CalledByNative
    boolean isReady();

    /** @return Buffer size in bytes. */
    @CalledByNative
    int getBufferSize();

    /** @return Sample rate in Hz. */
    @CalledByNative
    int getSampleRate();

    /** @return Number of audio channels. */
    @CalledByNative
    int getChannelCount();

    /** @return Estimated latency in milliseconds. */
    @CalledByNative
    int getLatency();

    /**
     * @return Estimated duration of audio data buffered but not yet played,
     *         in microseconds.
     */
    @CalledByNative
    long getBufferDurationUs();

    /**
     * @return The number of frames written to the sink since open.
     */
    @CalledByNative
    long getFramesWritten();

    /**
     * @return Playback head position in frames since open.
     */
    @CalledByNative
    int getPosition();
}
