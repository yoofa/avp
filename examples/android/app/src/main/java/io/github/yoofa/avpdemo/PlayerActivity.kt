package io.github.yoofa.avpdemo

import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.SurfaceHolder
import android.view.View
import android.view.WindowManager
import android.widget.SeekBar
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import io.github.yoofa.avp.AvpPlayer
import io.github.yoofa.avpdemo.databinding.ActivityPlayerBinding

/**
 * Fullscreen video player activity using AvpPlayer native SDK.
 */
class PlayerActivity : AppCompatActivity() {

    private lateinit var binding: ActivityPlayerBinding
    private var player: AvpPlayer? = null
    private val handler = Handler(Looper.getMainLooper())
    private var isUserSeeking = false

    companion object {
        private const val TAG = "PlayerActivity"
        const val EXTRA_FILE_PATH = "file_path"
        private const val CONTROL_HIDE_DELAY = 4000L
        private const val PROGRESS_UPDATE_INTERVAL = 500L
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // Fullscreen
        window.setFlags(
            WindowManager.LayoutParams.FLAG_FULLSCREEN,
            WindowManager.LayoutParams.FLAG_FULLSCREEN
        )
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        binding = ActivityPlayerBinding.inflate(layoutInflater)
        setContentView(binding.root)

        val filePath = intent.getStringExtra(EXTRA_FILE_PATH)
        if (filePath.isNullOrEmpty()) {
            Toast.makeText(this, "No file path provided", Toast.LENGTH_SHORT).show()
            finish()
            return
        }

        binding.tvTitle.text = filePath.substringAfterLast('/')

        setupSurface(filePath)
        setupControls()
    }

    private fun setupSurface(filePath: String) {
        binding.surfaceView.holder.addCallback(object : SurfaceHolder.Callback {
            override fun surfaceCreated(holder: SurfaceHolder) {
                Log.d(TAG, "Surface created, initializing player")
                initPlayer(filePath, holder)
            }

            override fun surfaceChanged(holder: SurfaceHolder, format: Int, w: Int, h: Int) {
                Log.d(TAG, "Surface changed: ${w}x${h}")
            }

            override fun surfaceDestroyed(holder: SurfaceHolder) {
                Log.d(TAG, "Surface destroyed, releasing player")
                releasePlayer()
            }
        })
    }

    private fun initPlayer(filePath: String, holder: SurfaceHolder) {
        try {
            player = AvpPlayer.create().apply {
                setDataSource(filePath)
                setSurface(holder.surface)

                setOnPreparedListener { p ->
                    Log.d(TAG, "Player prepared")
                    runOnUiThread {
                        binding.btnPlayPause.text = "⏸"
                    }
                    p.start()
                }

                setOnCompletionListener {
                    Log.d(TAG, "Playback completed")
                    runOnUiThread {
                        binding.btnPlayPause.text = "▶"
                        binding.seekBar.progress = 0
                    }
                }

                setOnErrorListener { _, errorCode ->
                    Log.e(TAG, "Player error: $errorCode")
                    runOnUiThread {
                        Toast.makeText(
                            this@PlayerActivity,
                            "Playback error: $errorCode",
                            Toast.LENGTH_SHORT
                        ).show()
                    }
                    true
                }

                prepare()
            }
        } catch (e: Exception) {
            Log.e(TAG, "Failed to create player", e)
            Toast.makeText(this, "Failed to initialize player: ${e.message}",
                Toast.LENGTH_LONG).show()
            finish()
        }
    }

    private fun setupControls() {
        // Tap to toggle controls
        binding.surfaceView.setOnClickListener {
            toggleControls()
        }

        binding.btnPlayPause.setOnClickListener {
            player?.let { p ->
                // Toggle play/pause
                binding.btnPlayPause.text = "▶"
                p.pause()
            }
        }

        binding.seekBar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                if (fromUser) {
                    binding.tvPosition.text = formatDuration(progress.toLong())
                }
            }

            override fun onStartTrackingTouch(seekBar: SeekBar?) {
                isUserSeeking = true
            }

            override fun onStopTrackingTouch(seekBar: SeekBar?) {
                isUserSeeking = false
                seekBar?.let {
                    player?.seekTo(it.progress)
                }
            }
        })

        binding.btnBack.setOnClickListener {
            finish()
        }

        // Initially show controls, then auto-hide
        showControls()
    }

    private fun toggleControls() {
        if (binding.controlsOverlay.visibility == View.VISIBLE) {
            hideControls()
        } else {
            showControls()
        }
    }

    private fun showControls() {
        binding.controlsOverlay.visibility = View.VISIBLE
        binding.topBar.visibility = View.VISIBLE
        handler.removeCallbacks(hideControlsRunnable)
        handler.postDelayed(hideControlsRunnable, CONTROL_HIDE_DELAY)
    }

    private fun hideControls() {
        binding.controlsOverlay.visibility = View.GONE
        binding.topBar.visibility = View.GONE
    }

    private val hideControlsRunnable = Runnable { hideControls() }

    private fun releasePlayer() {
        handler.removeCallbacksAndMessages(null)
        player?.release()
        player = null
    }

    override fun onPause() {
        super.onPause()
        player?.pause()
    }

    override fun onDestroy() {
        super.onDestroy()
        releasePlayer()
    }

    private fun formatDuration(ms: Long): String {
        val totalSeconds = ms / 1000
        val hours = totalSeconds / 3600
        val minutes = (totalSeconds % 3600) / 60
        val seconds = totalSeconds % 60
        return if (hours > 0) {
            String.format("%d:%02d:%02d", hours, minutes, seconds)
        } else {
            String.format("%02d:%02d", minutes, seconds)
        }
    }
}
