/*
 *  Copyright (C) 2025 youfa <vsyfar@gmail.com>
 *
 *  Distributed under terms of the GPLv2 license.
 */

package io.github.yoofa.avp;

import android.view.Surface;

import androidx.annotation.Nullable;

import org.jni_zero.CalledByNative;
import org.jni_zero.NativeMethods;

import io.github.yoofa.ContextUtils;
import io.github.yoofa.Logging;
import io.github.yoofa.media.VideoFrame;
import io.github.yoofa.media.VideoRenderer;

/**
 * Main media player class for the AVP player SDK.
 * Mirrors the native C++ Player interface.
 *
 * <p>Usage:
 * <pre>
 *   AvpPlayer player = AvpPlayer.create();
 *   player.setDataSource("/path/to/file.mp4");
 *   player.setOnPreparedListener(mp -> mp.start());
 *   player.prepare();
 * </pre>
 */
public class AvpPlayer {
    private static final String TAG = "AvpPlayer";

    static {
        System.loadLibrary("avp_android");
    }

    private long nativePlayer;
    private @Nullable OnPreparedListener onPreparedListener;
    private @Nullable OnCompletionListener onCompletionListener;
    private @Nullable OnErrorListener onErrorListener;
    private @Nullable VideoRenderer videoRenderer;

    /**
     * Listener for when the player is prepared and ready to play.
     */
    public interface OnPreparedListener {
        void onPrepared(AvpPlayer player);
    }

    /**
     * Listener for when playback completes.
     */
    public interface OnCompletionListener {
        void onCompletion(AvpPlayer player);
    }

    /**
     * Listener for playback errors.
     */
    public interface OnErrorListener {
        /**
         * @param player The player instance.
         * @param errorCode The error code from native.
         * @return true if the error was handled, false otherwise.
         */
        boolean onError(AvpPlayer player, int errorCode);
    }

    /**
     * Create a new AvpPlayer instance using default factories.
     */
    public static AvpPlayer create() {
        return new AvpPlayer();
    }

    private AvpPlayer() {
        this.nativePlayer = nativeInit();
        if (nativePlayer == 0) {
            throw new RuntimeException("Failed to create native player");
        }
    }

    /**
     * Set the data source from a file path or URL.
     *
     * @param path File path or URL string.
     */
    public void setDataSource(String path) {
        checkNotReleased();
        nativeSetDataSource(nativePlayer, path);
    }

    /**
     * Set the data source from a file descriptor.
     *
     * @param fd File descriptor.
     * @param offset Offset into the file.
     * @param length Length of data to use.
     */
    public void setDataSource(int fd, long offset, long length) {
        checkNotReleased();
        nativeSetDataSourceFd(nativePlayer, fd, offset, length);
    }

    /**
     * Set the video renderer for receiving decoded video frames.
     *
     * @param renderer The video renderer, or null to clear.
     */
    public void setVideoRenderer(@Nullable VideoRenderer renderer) {
        this.videoRenderer = renderer;
        checkNotReleased();
        nativeSetVideoRenderer(nativePlayer, renderer != null);
    }

    /**
     * Set an Android Surface for video output.
     *
     * @param surface The Surface to render video to, or null to clear.
     */
    public void setSurface(@Nullable Surface surface) {
        checkNotReleased();
        nativeSetSurface(nativePlayer, surface);
    }

    /**
     * Prepare the player for playback asynchronously.
     */
    public void prepare() {
        checkNotReleased();
        nativePrepare(nativePlayer);
    }

    /**
     * Start or resume playback.
     */
    public void start() {
        checkNotReleased();
        nativeStart(nativePlayer);
    }

    /**
     * Pause playback.
     */
    public void pause() {
        checkNotReleased();
        nativePause(nativePlayer);
    }

    /**
     * Resume playback after pause.
     */
    public void resume() {
        checkNotReleased();
        nativeResume(nativePlayer);
    }

    /**
     * Stop playback.
     */
    public void stop() {
        checkNotReleased();
        nativeStop(nativePlayer);
    }

    /**
     * Seek to a position in milliseconds.
     *
     * @param msec Position in milliseconds.
     */
    public void seekTo(int msec) {
        checkNotReleased();
        nativeSeekTo(nativePlayer, msec);
    }

    /**
     * Reset the player to its uninitialized state.
     */
    public void reset() {
        checkNotReleased();
        nativeReset(nativePlayer);
    }

    /**
     * Release all native resources. The player cannot be used after this.
     */
    public void release() {
        if (nativePlayer != 0) {
            nativeRelease(nativePlayer);
            nativePlayer = 0;
        }
    }

    public void setOnPreparedListener(@Nullable OnPreparedListener listener) {
        this.onPreparedListener = listener;
    }

    public void setOnCompletionListener(@Nullable OnCompletionListener listener) {
        this.onCompletionListener = listener;
    }

    public void setOnErrorListener(@Nullable OnErrorListener listener) {
        this.onErrorListener = listener;
    }

    // --- Callbacks from native code ---

    @CalledByNative
    private void onNativePrepared() {
        Logging.d(TAG, "onNativePrepared");
        if (onPreparedListener != null) {
            onPreparedListener.onPrepared(this);
        }
    }

    @CalledByNative
    private void onNativeCompletion() {
        Logging.d(TAG, "onNativeCompletion");
        if (onCompletionListener != null) {
            onCompletionListener.onCompletion(this);
        }
    }

    @CalledByNative
    private void onNativeError(int errorCode) {
        Logging.e(TAG, "onNativeError: " + errorCode);
        if (onErrorListener != null) {
            onErrorListener.onError(this, errorCode);
        }
    }

    @CalledByNative
    private void onNativeVideoFrame(VideoFrame frame) {
        if (videoRenderer != null) {
            videoRenderer.onFrame(frame);
        }
    }

    private void checkNotReleased() {
        if (nativePlayer == 0) {
            throw new IllegalStateException("Player has been released");
        }
    }

    // --- Native methods ---
    private native long nativeInit();
    private static native void nativeSetDataSource(long nativeAvpPlayerJni,
                                                    String path);
    private static native void nativeSetDataSourceFd(long nativeAvpPlayerJni,
                                                      int fd, long offset,
                                                      long length);
    private static native void nativeSetVideoRenderer(long nativeAvpPlayerJni,
                                                       boolean hasRenderer);
    private static native void nativeSetSurface(long nativeAvpPlayerJni,
                                                 Surface surface);
    private static native void nativePrepare(long nativeAvpPlayerJni);
    private static native void nativeStart(long nativeAvpPlayerJni);
    private static native void nativePause(long nativeAvpPlayerJni);
    private static native void nativeResume(long nativeAvpPlayerJni);
    private static native void nativeStop(long nativeAvpPlayerJni);
    private static native void nativeSeekTo(long nativeAvpPlayerJni, int msec);
    private static native void nativeReset(long nativeAvpPlayerJni);
    private static native void nativeRelease(long nativeAvpPlayerJni);
}
