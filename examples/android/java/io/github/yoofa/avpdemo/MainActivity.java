package io.github.yoofa.avpdemo;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.widget.Button;
import android.widget.EditText;
import android.widget.Toast;
import java.io.File;

/** Main activity with a simple file path input for media playback. */
public class MainActivity extends Activity {

  private static final int REQUEST_PERMISSION = 100;

  private EditText editFilePath;
  private Button btnPlay;

  @Override
  protected void onCreate(Bundle savedInstanceState) {
    super.onCreate(savedInstanceState);
    setContentView(R.layout.activity_main);

    editFilePath = findViewById(R.id.editFilePath);
    btnPlay = findViewById(R.id.btnPlay);

    editFilePath.setHint("/sdcard/Movies/test.mp4");

    btnPlay.setOnClickListener(
        v -> {
          String path = editFilePath.getText().toString().trim();
          if (path.isEmpty()) {
            Toast.makeText(this, "Please enter a file path", Toast.LENGTH_SHORT).show();
            return;
          }
          if (!new File(path).exists()) {
            Toast.makeText(this, "File not found: " + path, Toast.LENGTH_SHORT).show();
            return;
          }
          startPlayer(path);
        });

    handleIntent(getIntent());
    requestStoragePermission();
  }

  @Override
  protected void onNewIntent(Intent intent) {
    super.onNewIntent(intent);
    handleIntent(intent);
  }

  private void handleIntent(Intent intent) {
    if (intent == null || intent.getData() == null) return;
    String path = intent.getData().getPath();
    boolean audioOnly = intent.getData().getBooleanQueryParameter("audio_only", false);
    if (path != null) {
      editFilePath.setText(path);
      startPlayer(path, audioOnly);
    }
  }

  private void startPlayer(String path) {
    startPlayer(path, false);
  }

  private void startPlayer(String path, boolean audioOnly) {
    Intent intent = new Intent(this, PlayerActivity.class);
    intent.putExtra(PlayerActivity.EXTRA_FILE_PATH, path);
    intent.putExtra(PlayerActivity.EXTRA_AUDIO_ONLY, audioOnly);
    startActivity(intent);
  }

  private void requestStoragePermission() {
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
      if (checkSelfPermission(Manifest.permission.READ_MEDIA_VIDEO)
          != PackageManager.PERMISSION_GRANTED) {
        requestPermissions(
            new String[] {
              Manifest.permission.READ_MEDIA_VIDEO, Manifest.permission.READ_MEDIA_AUDIO
            },
            REQUEST_PERMISSION);
      }
    } else {
      if (checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE)
          != PackageManager.PERMISSION_GRANTED) {
        requestPermissions(
            new String[] {Manifest.permission.READ_EXTERNAL_STORAGE}, REQUEST_PERMISSION);
      }
    }
  }
}
