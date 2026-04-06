/*
 *  Copyright (C) 2025 youfa <vsyfar@gmail.com>
 *
 *  Distributed under terms of the GPLv2 license.
 */

package io.github.yoofa.avp;

import android.util.Log;
import android.view.Surface;
import androidx.annotation.Nullable;
import io.github.yoofa.media.AndroidJavaAudioDevice;
import io.github.yoofa.media.AudioDevice;
import io.github.yoofa.media.VideoFrame;
import io.github.yoofa.media.VideoRenderer;
import org.jni_zero.CalledByNative;
import org.jni_zero.JniType;
import org.jni_zero.NativeMethods;

/**
 * Main media player class for the AVP player SDK. Mirrors the native C++ Player interface.
 *
 * <p>Usage:
 *
 * <pre>
 *   AvpPlayer player = new AvpPlayer.Builder().build();
 *   player.setDataSource("/path/to/file.mp4");
 *   player.setOnPreparedListener(mp -> mp.start());
 *   player.prepare();
 * </pre>
 */
public class AvpPlayer {
  private static final String TAG = "AvpPlayer";

  /** Info code: buffering has started, player may pause. */
  public static final int MEDIA_INFO_BUFFERING_START = 701;

  /** Info code: buffering has ended, player may resume. */
  public static final int MEDIA_INFO_BUFFERING_END = 702;

  static {
    System.loadLibrary("avp_android");
  }

  private long nativePlayer;
  private @Nullable OnPreparedListener onPreparedListener;
  private @Nullable OnCompletionListener onCompletionListener;
  private @Nullable OnErrorListener onErrorListener;
  private @Nullable OnSeekCompleteListener onSeekCompleteListener;
  private @Nullable OnBufferingUpdateListener onBufferingUpdateListener;
  private @Nullable OnVideoSizeChangedListener onVideoSizeChangedListener;
  private @Nullable OnInfoListener onInfoListener;
  private @Nullable VideoRenderer videoRenderer;
  private boolean looping;

  /** Listener for when the player is prepared and ready to play. */
  public interface OnPreparedListener {
    /**
     * Called when the player is prepared and ready for playback.
     *
     * @param player The player instance.
     */
    void onPrepared(AvpPlayer player);
  }

  /** Listener for when playback completes. */
  public interface OnCompletionListener {
    /**
     * Called when the end of media is reached during playback.
     *
     * @param player The player instance.
     */
    void onCompletion(AvpPlayer player);
  }

  /** Listener for playback errors. */
  public interface OnErrorListener {
    /**
     * Called to indicate an error.
     *
     * @param player The player instance.
     * @param errorCode The error code from native.
     * @return true if the error was handled, false otherwise.
     */
    boolean onError(AvpPlayer player, int errorCode);
  }

  /** Listener for when a seek operation completes. */
  public interface OnSeekCompleteListener {
    /**
     * Called to indicate that a seek operation has completed.
     *
     * @param player The player instance.
     */
    void onSeekComplete(AvpPlayer player);
  }

  /** Listener for buffering progress updates. */
  public interface OnBufferingUpdateListener {
    /**
     * Called to update the buffering percentage.
     *
     * @param player The player instance.
     * @param percent The buffering percentage (0-100).
     */
    void onBufferingUpdate(AvpPlayer player, int percent);
  }

  /** Listener for video size changes. */
  public interface OnVideoSizeChangedListener {
    /**
     * Called when the video size is first known or changes.
     *
     * @param player The player instance.
     * @param width The new video width in pixels.
     * @param height The new video height in pixels.
     */
    void onVideoSizeChanged(AvpPlayer player, int width, int height);
  }

  /** Listener for informational events. */
  public interface OnInfoListener {
    /**
     * Called to indicate an info or a warning.
     *
     * @param player The player instance.
     * @param what The type of info or warning.
     * @param extra An extra code, specific to the info.
     * @return true if the info was handled, false otherwise.
     */
    boolean onInfo(AvpPlayer player, int what, int extra);
  }

  /**
   * Create a new AvpPlayer instance using default factories and the default {@link
   * AndroidJavaAudioDevice} for audio output.
   *
   * @return A new AvpPlayer ready for use.
   * @throws RuntimeException if native player creation fails.
   */
  public static AvpPlayer create() {
    return new Builder().setAudioDevice(new AndroidJavaAudioDevice()).build();
  }

  /**
   * Create a new AvpPlayer instance with a custom {@link AudioDevice}.
   *
   * @param audioDevice The audio device to use for audio output.
   * @return A new AvpPlayer ready for use.
   * @throws RuntimeException if native player creation fails.
   */
  public static AvpPlayer create(AudioDevice audioDevice) {
    return new Builder().setAudioDevice(audioDevice).build();
  }

  private AvpPlayer(Builder builder) {
    AudioDevice audioDevice = builder.audioDevice;
    if (audioDevice == null) {
      audioDevice = new AndroidJavaAudioDevice();
    }
    this.nativePlayer =
        AvpPlayerJni.get()
            .init(
                this,
                audioDevice,
                builder.syncEnabled,
                builder.audioPassthroughPolicy,
                builder.audioOnly);
    if (nativePlayer == 0) {
      throw new RuntimeException("Failed to create native player");
    }
  }

  // --- Data source ---

  /**
   * Set the data source from a file path or URL.
   *
   * @param path File path or URL string.
   */
  public void setDataSource(String path) {
    checkNotReleased();
    AvpPlayerJni.get().setDataSource(nativePlayer, path);
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
    AvpPlayerJni.get().setDataSourceFd(nativePlayer, fd, offset, length);
  }

  // --- Video output ---

  /**
   * Set the video renderer for receiving decoded video frames.
   *
   * @param renderer The video renderer, or null to clear.
   */
  public void setVideoRenderer(@Nullable VideoRenderer renderer) {
    this.videoRenderer = renderer;
    checkNotReleased();
    AvpPlayerJni.get().setVideoRenderer(nativePlayer, renderer != null);
  }

  /**
   * Set an Android Surface for video output.
   *
   * @param surface The Surface to render video to, or null to clear.
   */
  public void setSurface(@Nullable Surface surface) {
    checkNotReleased();
    AvpPlayerJni.get().setSurface(nativePlayer, surface);
  }

  // --- Playback control ---

  /**
   * Prepare the player for playback asynchronously. The OnPreparedListener will be called when
   * ready.
   */
  public void prepare() {
    checkNotReleased();
    AvpPlayerJni.get().prepare(nativePlayer);
  }

  /** Start or resume playback. */
  public void start() {
    checkNotReleased();
    AvpPlayerJni.get().start(nativePlayer);
  }

  /** Pause playback. */
  public void pause() {
    checkNotReleased();
    AvpPlayerJni.get().pause(nativePlayer);
  }

  /** Resume playback after pause. */
  public void resume() {
    checkNotReleased();
    AvpPlayerJni.get().resume(nativePlayer);
  }

  /** Stop playback. */
  public void stop() {
    checkNotReleased();
    AvpPlayerJni.get().stop(nativePlayer);
  }

  /**
   * Seek to a position in milliseconds using the default seek mode.
   *
   * @param msec Position in milliseconds.
   */
  public void seekTo(int msec) {
    checkNotReleased();
    AvpPlayerJni.get().seekTo(nativePlayer, msec, SeekMode.SEEK_PREVIOUS_SYNC.getNativeValue());
  }

  /**
   * Seek to a position in milliseconds with a specific seek mode.
   *
   * @param msec Position in milliseconds.
   * @param mode The seek mode to use.
   */
  public void seekTo(int msec, SeekMode mode) {
    checkNotReleased();
    AvpPlayerJni.get().seekTo(nativePlayer, msec, mode.getNativeValue());
  }

  /**
   * Reset the player to its uninitialized state. After reset, you must set a data source and
   * prepare again.
   */
  public void reset() {
    checkNotReleased();
    AvpPlayerJni.get().reset(nativePlayer);
  }

  /** Release all native resources. The player cannot be used after this. */
  public void release() {
    if (nativePlayer != 0) {
      AvpPlayerJni.get().release(nativePlayer);
      nativePlayer = 0;
    }
  }

  // --- State queries ---

  /**
   * Get the duration of the media content.
   *
   * @return Duration in milliseconds, or -1 if not available.
   */
  public int getDuration() {
    checkNotReleased();
    return AvpPlayerJni.get().getDuration(nativePlayer);
  }

  /**
   * Get the current playback position.
   *
   * @return Current position in milliseconds.
   */
  public int getCurrentPosition() {
    checkNotReleased();
    return AvpPlayerJni.get().getCurrentPosition(nativePlayer);
  }

  /**
   * Check whether the player is currently playing.
   *
   * @return true if the player is playing, false otherwise.
   */
  public boolean isPlaying() {
    checkNotReleased();
    return AvpPlayerJni.get().isPlaying(nativePlayer);
  }

  /**
   * Get the width of the video.
   *
   * @return The video width in pixels, or 0 if not available.
   */
  public int getVideoWidth() {
    checkNotReleased();
    return AvpPlayerJni.get().getVideoWidth(nativePlayer);
  }

  /**
   * Get the height of the video.
   *
   * @return The video height in pixels, or 0 if not available.
   */
  public int getVideoHeight() {
    checkNotReleased();
    return AvpPlayerJni.get().getVideoHeight(nativePlayer);
  }

  // --- Playback rate ---

  /**
   * Set the playback rate.
   *
   * @param rate Playback rate (1.0 = normal, 2.0 = double speed, 0.5 = half speed).
   */
  public void setPlaybackRate(float rate) {
    checkNotReleased();
    AvpPlayerJni.get().setPlaybackRate(nativePlayer, rate);
  }

  /**
   * Get the current playback rate.
   *
   * @return The current playback rate.
   */
  public float getPlaybackRate() {
    checkNotReleased();
    return AvpPlayerJni.get().getPlaybackRate(nativePlayer);
  }

  // --- Volume ---

  /**
   * Set the volume for left and right audio channels.
   *
   * @param leftVolume Left channel volume (0.0 to 1.0).
   * @param rightVolume Right channel volume (0.0 to 1.0).
   */
  public void setVolume(float leftVolume, float rightVolume) {
    checkNotReleased();
    AvpPlayerJni.get().setVolume(nativePlayer, leftVolume, rightVolume);
  }

  // --- Track management ---

  /**
   * Get the number of tracks in the current media.
   *
   * @return The number of tracks.
   */
  public int getTrackCount() {
    checkNotReleased();
    return AvpPlayerJni.get().getTrackCount(nativePlayer);
  }

  /**
   * Get information about a specific track.
   *
   * @param index The track index (0-based).
   * @return TrackInfo for the track, or null if index is invalid.
   */
  @Nullable
  public TrackInfo getTrackInfo(int index) {
    checkNotReleased();
    return AvpPlayerJni.get().getTrackInfo(nativePlayer, index);
  }

  /**
   * Select or deselect a track.
   *
   * @param index The track index.
   * @param select true to select, false to deselect.
   */
  public void selectTrack(int index, boolean select) {
    checkNotReleased();
    AvpPlayerJni.get().selectTrack(nativePlayer, index, select);
  }

  // --- Looping ---

  /** Audio passthrough policy constants. Must match native AudioPassthroughPolicy enum values. */
  public static final int PASSTHROUGH_ALWAYS_PCM = 0;

  public static final int PASSTHROUGH_PREFER_PASSTHROUGH = 1;
  public static final int PASSTHROUGH_AUTO = 2;

  /** Builder for player instances and init-only configuration. */
  public static final class Builder {
    private @Nullable AudioDevice audioDevice;
    private boolean syncEnabled = true;
    private int audioPassthroughPolicy;
    private boolean audioOnly;

    public Builder setAudioDevice(@Nullable AudioDevice audioDevice) {
      this.audioDevice = audioDevice;
      return this;
    }

    public Builder setSyncEnabled(boolean syncEnabled) {
      this.syncEnabled = syncEnabled;
      return this;
    }

    public Builder setAudioPassthroughPolicy(int audioPassthroughPolicy) {
      this.audioPassthroughPolicy = audioPassthroughPolicy;
      return this;
    }

    public Builder setAudioOnly(boolean audioOnly) {
      this.audioOnly = audioOnly;
      return this;
    }

    public AvpPlayer build() {
      return new AvpPlayer(this);
    }
  }

  /**
   * Set whether the player should loop playback.
   *
   * @param looping true to enable looping, false to disable.
   */
  public void setLooping(boolean looping) {
    this.looping = looping;
  }

  /**
   * Check whether looping is enabled.
   *
   * @return true if looping is enabled.
   */
  public boolean isLooping() {
    return looping;
  }

  // --- Listeners ---

  public void setOnPreparedListener(@Nullable OnPreparedListener listener) {
    this.onPreparedListener = listener;
  }

  public void setOnCompletionListener(@Nullable OnCompletionListener listener) {
    this.onCompletionListener = listener;
  }

  public void setOnErrorListener(@Nullable OnErrorListener listener) {
    this.onErrorListener = listener;
  }

  public void setOnSeekCompleteListener(@Nullable OnSeekCompleteListener listener) {
    this.onSeekCompleteListener = listener;
  }

  public void setOnBufferingUpdateListener(@Nullable OnBufferingUpdateListener listener) {
    this.onBufferingUpdateListener = listener;
  }

  public void setOnVideoSizeChangedListener(@Nullable OnVideoSizeChangedListener listener) {
    this.onVideoSizeChangedListener = listener;
  }

  public void setOnInfoListener(@Nullable OnInfoListener listener) {
    this.onInfoListener = listener;
  }

  // --- Callbacks from native code ---

  @CalledByNative
  private void onNativePrepared(int error) {
    Log.i(TAG, "onNativePrepared, error=" + error);
    if (onPreparedListener != null && error == 0) {
      Log.i(TAG, "onNativePrepared: calling onPreparedListener");
      onPreparedListener.onPrepared(this);
      Log.i(TAG, "onNativePrepared: onPreparedListener returned");
    } else if (error != 0 && onErrorListener != null) {
      onErrorListener.onError(this, error);
    }
  }

  @CalledByNative
  private void onNativeCompletion() {
    Log.i(TAG, "onNativeCompletion");
    if (looping) {
      seekTo(0);
      start();
      return;
    }
    if (onCompletionListener != null) {
      onCompletionListener.onCompletion(this);
    }
  }

  @CalledByNative
  private void onNativeError(int errorCode) {
    Log.e(TAG, "onNativeError: " + errorCode);
    if (onErrorListener != null) {
      onErrorListener.onError(this, errorCode);
    }
  }

  @CalledByNative
  private void onNativeSeekComplete() {
    Log.i(TAG, "onNativeSeekComplete");
    if (onSeekCompleteListener != null) {
      onSeekCompleteListener.onSeekComplete(this);
    }
  }

  @CalledByNative
  private void onNativeBufferingUpdate(int percent) {
    if (onBufferingUpdateListener != null) {
      onBufferingUpdateListener.onBufferingUpdate(this, percent);
    }
  }

  @CalledByNative
  private void onNativeVideoSizeChanged(int width, int height) {
    Log.i(TAG, "onNativeVideoSizeChanged: " + width + "x" + height);
    if (onVideoSizeChangedListener != null) {
      onVideoSizeChangedListener.onVideoSizeChanged(this, width, height);
    }
  }

  @CalledByNative
  private void onNativeInfo(int what, int extra) {
    if (onInfoListener != null) {
      onInfoListener.onInfo(this, what, extra);
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

  // --- Native methods via jni_zero proxy ---
  @NativeMethods
  interface Natives {
    long init(
        AvpPlayer caller,
        AudioDevice audioDevice,
        boolean syncEnabled,
        int audioPassthroughPolicy,
        boolean audioOnly);

    void setDataSource(long nativeAvpPlayerJni, @JniType("std::string") String path);

    void setDataSourceFd(long nativeAvpPlayerJni, int fd, long offset, long length);

    void setVideoRenderer(long nativeAvpPlayerJni, boolean hasRenderer);

    void setSurface(long nativeAvpPlayerJni, Surface surface);

    void prepare(long nativeAvpPlayerJni);

    void start(long nativeAvpPlayerJni);

    void pause(long nativeAvpPlayerJni);

    void resume(long nativeAvpPlayerJni);

    void stop(long nativeAvpPlayerJni);

    void seekTo(long nativeAvpPlayerJni, int msec, int mode);

    void reset(long nativeAvpPlayerJni);

    void release(long nativeAvpPlayerJni);

    int getDuration(long nativeAvpPlayerJni);

    int getCurrentPosition(long nativeAvpPlayerJni);

    boolean isPlaying(long nativeAvpPlayerJni);

    int getVideoWidth(long nativeAvpPlayerJni);

    int getVideoHeight(long nativeAvpPlayerJni);

    void setPlaybackRate(long nativeAvpPlayerJni, float rate);

    float getPlaybackRate(long nativeAvpPlayerJni);

    void setVolume(long nativeAvpPlayerJni, float leftVolume, float rightVolume);

    int getTrackCount(long nativeAvpPlayerJni);

    TrackInfo getTrackInfo(long nativeAvpPlayerJni, int index);

    void selectTrack(long nativeAvpPlayerJni, int index, boolean select);
  }
}
