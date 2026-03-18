package io.github.yoofa.avpdemo;

import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowManager;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.Toast;

import io.github.yoofa.avp.AvpPlayer;

/**
 * Fullscreen video player activity using the AVP native SDK.
 */
public class PlayerActivity extends Activity {

    private static final String TAG = "PlayerActivity";
    public static final String EXTRA_FILE_PATH = "file_path";
    private static final long CONTROL_HIDE_DELAY_MS = 4000;

    private SurfaceView surfaceView;
    private View topBar;
    private View controlsOverlay;
    private TextView tvTitle;
    private TextView tvPosition;
    private SeekBar seekBar;
    private TextView btnPlayPause;
    private View btnBack;

    private AvpPlayer player;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private boolean isPlaying;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().setFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN,
                WindowManager.LayoutParams.FLAG_FULLSCREEN);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        setContentView(R.layout.activity_player);

        String filePath = getIntent().getStringExtra(EXTRA_FILE_PATH);
        if (filePath == null || filePath.isEmpty()) {
            Toast.makeText(this, "No file path provided", Toast.LENGTH_SHORT).show();
            finish();
            return;
        }

        surfaceView = findViewById(R.id.surfaceView);
        topBar = findViewById(R.id.topBar);
        controlsOverlay = findViewById(R.id.controlsOverlay);
        tvTitle = findViewById(R.id.tvTitle);
        tvPosition = findViewById(R.id.tvPosition);
        seekBar = findViewById(R.id.seekBar);
        btnPlayPause = findViewById(R.id.btnPlayPause);
        btnBack = findViewById(R.id.btnBack);

        tvTitle.setText(filePath.substring(filePath.lastIndexOf('/') + 1));

        setupSurface(filePath);
        setupControls();
    }

    private void setupSurface(String filePath) {
        surfaceView.getHolder().addCallback(new SurfaceHolder.Callback() {
            @Override
            public void surfaceCreated(SurfaceHolder holder) {
                Log.d(TAG, "Surface created");
                initPlayer(filePath, holder);
            }

            @Override
            public void surfaceChanged(SurfaceHolder holder, int format, int w, int h) {
                Log.d(TAG, "Surface changed: " + w + "x" + h);
            }

            @Override
            public void surfaceDestroyed(SurfaceHolder holder) {
                Log.d(TAG, "Surface destroyed");
                releasePlayer();
            }
        });
    }

    private void initPlayer(String filePath, SurfaceHolder holder) {
        try {
            player = AvpPlayer.create();
            player.setDataSource(filePath);
            player.setSurface(holder.getSurface());

            player.setOnPreparedListener(p -> {
                Log.d(TAG, "Player prepared");
                runOnUiThread(() -> updatePlayPauseButton(true));
                p.start();
                isPlaying = true;
            });

            player.setOnCompletionListener(p -> {
                Log.d(TAG, "Playback completed");
                isPlaying = false;
                runOnUiThread(() -> {
                    updatePlayPauseButton(false);
                    seekBar.setProgress(0);
                });
            });

            player.setOnErrorListener((p, errorCode) -> {
                Log.e(TAG, "Player error: " + errorCode);
                runOnUiThread(() -> Toast.makeText(PlayerActivity.this,
                        "Playback error: " + errorCode, Toast.LENGTH_SHORT).show());
                return true;
            });

            player.prepare();

        } catch (Exception e) {
            Log.e(TAG, "Failed to init player", e);
            Toast.makeText(this, "Failed to initialize player: " + e.getMessage(),
                    Toast.LENGTH_LONG).show();
            finish();
        }
    }

    private void setupControls() {
        surfaceView.setOnClickListener(v -> toggleControls());

        btnPlayPause.setOnClickListener(v -> {
            if (player == null) return;
            if (isPlaying) {
                player.pause();
                isPlaying = false;
                updatePlayPauseButton(false);
            } else {
                player.resume();
                isPlaying = true;
                updatePlayPauseButton(true);
            }
            showControls();
        });

        seekBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                if (fromUser) tvPosition.setText(formatDuration(progress));
            }

            @Override
            public void onStartTrackingTouch(SeekBar seekBar) { }

            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {
                if (player != null) player.seekTo(seekBar.getProgress());
            }
        });

        btnBack.setOnClickListener(v -> finish());

        showControls();
    }

    private void updatePlayPauseButton(boolean playing) {
        btnPlayPause.setText(playing ? "⏸" : "▶");
    }

    private void toggleControls() {
        if (controlsOverlay.getVisibility() == View.VISIBLE) {
            hideControls();
        } else {
            showControls();
        }
    }

    private void showControls() {
        controlsOverlay.setVisibility(View.VISIBLE);
        topBar.setVisibility(View.VISIBLE);
        handler.removeCallbacks(hideControlsRunnable);
        handler.postDelayed(hideControlsRunnable, CONTROL_HIDE_DELAY_MS);
    }

    private void hideControls() {
        controlsOverlay.setVisibility(View.GONE);
        topBar.setVisibility(View.GONE);
    }

    private final Runnable hideControlsRunnable = this::hideControls;

    private void releasePlayer() {
        handler.removeCallbacksAndMessages(null);
        if (player != null) {
            player.release();
            player = null;
        }
    }

    @Override
    protected void onPause() {
        super.onPause();
        if (player != null) {
            player.pause();
            isPlaying = false;
            runOnUiThread(() -> updatePlayPauseButton(false));
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        releasePlayer();
    }

    private String formatDuration(long ms) {
        long total = ms / 1000;
        long h = total / 3600;
        long m = (total % 3600) / 60;
        long s = total % 60;
        if (h > 0) return String.format("%d:%02d:%02d", h, m, s);
        return String.format("%02d:%02d", m, s);
    }
}
