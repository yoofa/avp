package io.github.yoofa.avpdemo

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import io.github.yoofa.avpdemo.databinding.ActivityMainBinding
import java.io.File

/**
 * Main activity with a simple file path input for playing media.
 */
class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding

    companion object {
        private const val REQUEST_PERMISSION = 100
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        setSupportActionBar(binding.toolbar)
        supportActionBar?.title = "AVP Demo"

        // Set default path hint
        binding.editFilePath.hint = "/sdcard/Movies/test.mp4"

        binding.btnPlay.setOnClickListener {
            val path = binding.editFilePath.text.toString().trim()
            if (path.isEmpty()) {
                Toast.makeText(this, "Please enter a file path", Toast.LENGTH_SHORT).show()
                return@setOnClickListener
            }
            if (!File(path).exists()) {
                Toast.makeText(this, "File not found: $path", Toast.LENGTH_SHORT).show()
                return@setOnClickListener
            }
            startPlayer(path)
        }

        // Handle intent (e.g., opening a video file)
        handleIntent(intent)

        requestPermissions()
    }

    override fun onNewIntent(intent: Intent?) {
        super.onNewIntent(intent)
        intent?.let { handleIntent(it) }
    }

    private fun handleIntent(intent: Intent) {
        val data = intent.data ?: return
        val path = data.path
        if (path != null) {
            binding.editFilePath.setText(path)
            startPlayer(path)
        }
    }

    private fun startPlayer(path: String) {
        val intent = Intent(this, PlayerActivity::class.java).apply {
            putExtra(PlayerActivity.EXTRA_FILE_PATH, path)
        }
        startActivity(intent)
    }

    private fun requestPermissions() {
        val permissions = mutableListOf<String>()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.READ_MEDIA_VIDEO)
                != PackageManager.PERMISSION_GRANTED) {
                permissions.add(Manifest.permission.READ_MEDIA_VIDEO)
                permissions.add(Manifest.permission.READ_MEDIA_AUDIO)
            }
        } else {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.READ_EXTERNAL_STORAGE)
                != PackageManager.PERMISSION_GRANTED) {
                permissions.add(Manifest.permission.READ_EXTERNAL_STORAGE)
            }
        }
        if (permissions.isNotEmpty()) {
            ActivityCompat.requestPermissions(this, permissions.toTypedArray(), REQUEST_PERMISSION)
        }
    }
}
