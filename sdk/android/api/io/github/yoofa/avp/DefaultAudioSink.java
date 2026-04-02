/*
 *  Copyright (C) 2025 youfa <vsyfar@gmail.com>
 *
 *  Distributed under terms of the GPLv2 license.
 */

package io.github.yoofa.avp;

import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioTrack;
import android.util.Log;

/**
 * Default {@link AudioSink} implementation backed by Android {@link AudioTrack}
 * in streaming mode ({@code MODE_STREAM}).
 *
 * <p>Currently supports PCM output only. Offload and tunnel modes will be added
 * in a future iteration.
 */
public class DefaultAudioSink implements AudioSink {

    private static final String TAG = "DefaultAudioSink";

    private AudioTrack audioTrack;
    private int sampleRate;
    private int channelCount;
    private long framesWritten;
    private int frameSize;

    @Override
    public boolean open(int sampleRate, int channelCount, int encoding) {
        Log.i(TAG, "open: sampleRate=" + sampleRate + " channels=" + channelCount
                + " encoding=" + encoding);
        close();

        this.sampleRate = sampleRate;
        this.channelCount = channelCount;
        this.framesWritten = 0;

        int androidEncoding = toAndroidEncoding(encoding);
        if (androidEncoding == AudioFormat.ENCODING_INVALID) {
            Log.e(TAG, "Unsupported encoding: " + encoding);
            return false;
        }

        int channelMask = channelCountToMask(channelCount);
        int bytesPerSample = bytesPerSampleForEncoding(androidEncoding);
        this.frameSize = channelCount * bytesPerSample;

        int minBufferSize = AudioTrack.getMinBufferSize(sampleRate, channelMask,
                androidEncoding);
        if (minBufferSize <= 0) {
            Log.e(TAG, "getMinBufferSize failed: " + minBufferSize);
            return false;
        }

        // Use 4x min buffer for smooth streaming playback
        int bufferSize = minBufferSize * 4;

        try {
            AudioAttributes attrs = new AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_MEDIA)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                    .build();

            AudioFormat format = new AudioFormat.Builder()
                    .setSampleRate(sampleRate)
                    .setChannelMask(channelMask)
                    .setEncoding(androidEncoding)
                    .build();

            audioTrack = new AudioTrack(attrs, format, bufferSize,
                    AudioTrack.MODE_STREAM,
                    android.media.AudioManager.AUDIO_SESSION_ID_GENERATE);

            if (audioTrack.getState() != AudioTrack.STATE_INITIALIZED) {
                Log.e(TAG, "AudioTrack failed to initialize");
                audioTrack.release();
                audioTrack = null;
                return false;
            }

            Log.i(TAG, "AudioTrack created: bufferSize=" + bufferSize
                    + " minBufferSize=" + minBufferSize
                    + " sampleRate=" + audioTrack.getSampleRate());
            return true;
        } catch (Exception e) {
            Log.e(TAG, "Failed to create AudioTrack", e);
            audioTrack = null;
            return false;
        }
    }

    @Override
    public int write(byte[] data, int offset, int size) {
        if (audioTrack == null) {
            return -1;
        }
        int written = audioTrack.write(data, offset, size);
        if (written > 0 && frameSize > 0) {
            framesWritten += written / frameSize;
        }
        return written;
    }

    @Override
    public void start() {
        if (audioTrack != null) {
            Log.i(TAG, "start");
            audioTrack.play();
        }
    }

    @Override
    public void stop() {
        if (audioTrack != null) {
            Log.i(TAG, "stop");
            try {
                audioTrack.stop();
            } catch (IllegalStateException e) {
                Log.w(TAG, "stop failed", e);
            }
            framesWritten = 0;
        }
    }

    @Override
    public void flush() {
        if (audioTrack != null) {
            Log.i(TAG, "flush");
            audioTrack.flush();
            framesWritten = 0;
        }
    }

    @Override
    public void pause() {
        if (audioTrack != null) {
            Log.i(TAG, "pause");
            try {
                audioTrack.pause();
            } catch (IllegalStateException e) {
                Log.w(TAG, "pause failed", e);
            }
        }
    }

    @Override
    public void close() {
        if (audioTrack != null) {
            Log.i(TAG, "close");
            try {
                audioTrack.stop();
            } catch (IllegalStateException ignored) {
            }
            audioTrack.release();
            audioTrack = null;
            framesWritten = 0;
        }
    }

    @Override
    public boolean isReady() {
        return audioTrack != null
                && audioTrack.getState() == AudioTrack.STATE_INITIALIZED;
    }

    @Override
    public int getBufferSize() {
        if (audioTrack == null) {
            return 0;
        }
        return audioTrack.getBufferSizeInFrames() * frameSize;
    }

    @Override
    public int getSampleRate() {
        return sampleRate;
    }

    @Override
    public int getChannelCount() {
        return channelCount;
    }

    @Override
    public int getLatency() {
        if (audioTrack == null || sampleRate == 0) {
            return 0;
        }
        int bufferFrames = audioTrack.getBufferSizeInFrames();
        return (int) ((long) bufferFrames * 1000 / sampleRate);
    }

    @Override
    public long getBufferDurationUs() {
        if (audioTrack == null || sampleRate == 0) {
            return 0;
        }
        long playbackHead = audioTrack.getPlaybackHeadPosition() & 0xFFFFFFFFL;
        long buffered = framesWritten - playbackHead;
        if (buffered <= 0) {
            return 0;
        }
        return buffered * 1_000_000L / sampleRate;
    }

    @Override
    public long getFramesWritten() {
        return framesWritten;
    }

    @Override
    public int getPosition() {
        if (audioTrack == null) {
            return 0;
        }
        return audioTrack.getPlaybackHeadPosition();
    }

    // --- Private helpers ---

    private static int toAndroidEncoding(int avpEncoding) {
        switch (avpEncoding) {
            case AudioSink.ENCODING_PCM_16BIT:
                return AudioFormat.ENCODING_PCM_16BIT;
            case AudioSink.ENCODING_PCM_FLOAT:
                return AudioFormat.ENCODING_PCM_FLOAT;
            case AudioSink.ENCODING_PCM_8BIT:
                return AudioFormat.ENCODING_PCM_8BIT;
            case AudioSink.ENCODING_PCM_32BIT:
                // API 31+; fall back to float for older
                return AudioFormat.ENCODING_PCM_32BIT;
            default:
                return AudioFormat.ENCODING_INVALID;
        }
    }

    private static int channelCountToMask(int channelCount) {
        switch (channelCount) {
            case 1:
                return AudioFormat.CHANNEL_OUT_MONO;
            case 2:
                return AudioFormat.CHANNEL_OUT_STEREO;
            case 4:
                return AudioFormat.CHANNEL_OUT_QUAD;
            case 6:
                return AudioFormat.CHANNEL_OUT_5POINT1;
            case 8:
                return AudioFormat.CHANNEL_OUT_7POINT1_SURROUND;
            default:
                // Fallback: construct from channel index mask
                return (1 << channelCount) - 1;
        }
    }

    private static int bytesPerSampleForEncoding(int androidEncoding) {
        switch (androidEncoding) {
            case AudioFormat.ENCODING_PCM_8BIT:
                return 1;
            case AudioFormat.ENCODING_PCM_16BIT:
                return 2;
            case AudioFormat.ENCODING_PCM_FLOAT:
            case AudioFormat.ENCODING_PCM_32BIT:
                return 4;
            default:
                return 2;
        }
    }
}
