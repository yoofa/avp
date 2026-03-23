package io.github.yoofa.avpdemo;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.util.Log;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.BaseAdapter;
import android.widget.CheckBox;
import android.widget.HorizontalScrollView;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * File browser activity modeled after SimpleBrowser.
 * Displays storage volumes on the home screen and navigates into directories
 * to find media files for playback.
 */
public class FileBrowserActivity extends Activity {

    private static final String TAG = "FileBrowserActivity";
    private static final int REQUEST_PERMISSION = 200;
    private static final int REQUEST_MANAGE_STORAGE = 201;
    private static final int MAX_SCAN_DEPTH = 5;

    // Views
    private TextView btnBack;
    private HorizontalScrollView breadcrumbScroll;
    private LinearLayout breadcrumbContainer;
    private CheckBox cbMediaOnly;
    private ListView listView;
    private TextView tvEmpty;
    private TextView tvStatus;

    // State
    private boolean isHome = true;
    private String currentPath = "";
    private boolean showMediaOnly = true;
    private List<StorageUtils.StorageItem> storageItems = new ArrayList<>();
    private List<FileItem> fileItems = new ArrayList<>();
    private final Map<String, Boolean> mediaFolderCache = new HashMap<>();

    // Breadcrumb model
    private static class BreadcrumbItem {
        final String name;
        final String path;
        BreadcrumbItem(String name, String path) {
            this.name = name;
            this.path = path;
        }
    }
    private List<BreadcrumbItem> breadcrumbs = new ArrayList<>();

    // ──────────────────────────────────────────────────────────────────────
    // Lifecycle
    // ──────────────────────────────────────────────────────────────────────

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_file_browser);

        btnBack = findViewById(R.id.btnBack);
        breadcrumbScroll = findViewById(R.id.breadcrumbScroll);
        breadcrumbContainer = findViewById(R.id.breadcrumbContainer);
        cbMediaOnly = findViewById(R.id.cbMediaOnly);
        listView = findViewById(R.id.listView);
        tvEmpty = findViewById(R.id.tvEmpty);
        tvStatus = findViewById(R.id.tvStatus);

        btnBack.setOnClickListener(v -> onBackPressed());

        cbMediaOnly.setChecked(showMediaOnly);
        cbMediaOnly.setOnCheckedChangeListener((btn, checked) -> {
            showMediaOnly = checked;
            mediaFolderCache.clear();
            if (!isHome) {
                loadFiles(currentPath);
            }
        });

        listView.setOnItemClickListener((parent, view, position, id) -> {
            if (isHome) {
                if (position < storageItems.size()) {
                    navigateTo(storageItems.get(position).path);
                }
            } else {
                if (position < fileItems.size()) {
                    FileItem item = fileItems.get(position);
                    if (item.isDirectory) {
                        navigateTo(item.path);
                    } else {
                        openMediaFile(item.path);
                    }
                }
            }
        });

        requestStoragePermission();
        loadStorageList();
    }

    @Override
    public void onBackPressed() {
        if (!navigateUp()) {
            super.onBackPressed();
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions,
                                           int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == REQUEST_PERMISSION) {
            boolean granted = grantResults.length > 0
                    && grantResults[0] == PackageManager.PERMISSION_GRANTED;
            if (granted) {
                loadStorageList();
            } else {
                Toast.makeText(this, "Storage permission required to browse files",
                        Toast.LENGTH_SHORT).show();
            }
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_MANAGE_STORAGE) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R
                    && Environment.isExternalStorageManager()) {
                loadStorageList();
            }
        }
    }

    // ──────────────────────────────────────────────────────────────────────
    // Permissions
    // ──────────────────────────────────────────────────────────────────────

    private void requestStoragePermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!Environment.isExternalStorageManager()) {
                try {
                    Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
                    intent.setData(Uri.parse("package:" + getPackageName()));
                    startActivityForResult(intent, REQUEST_MANAGE_STORAGE);
                } catch (Exception e) {
                    Intent intent = new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION);
                    startActivityForResult(intent, REQUEST_MANAGE_STORAGE);
                }
            }
        } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            if (checkSelfPermission(Manifest.permission.READ_MEDIA_VIDEO)
                    != PackageManager.PERMISSION_GRANTED) {
                requestPermissions(new String[]{
                        Manifest.permission.READ_MEDIA_VIDEO,
                        Manifest.permission.READ_MEDIA_AUDIO
                }, REQUEST_PERMISSION);
            }
        } else {
            if (checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE)
                    != PackageManager.PERMISSION_GRANTED) {
                requestPermissions(new String[]{
                        Manifest.permission.READ_EXTERNAL_STORAGE
                }, REQUEST_PERMISSION);
            }
        }
    }

    // ──────────────────────────────────────────────────────────────────────
    // Navigation
    // ──────────────────────────────────────────────────────────────────────

    private void loadStorageList() {
        isHome = true;
        currentPath = "";
        storageItems = StorageUtils.getStorageList(this);

        breadcrumbs.clear();
        breadcrumbs.add(new BreadcrumbItem("Home", ""));
        updateBreadcrumbsUI();

        listView.setAdapter(new StorageAdapter());
        tvEmpty.setVisibility(storageItems.isEmpty() ? View.VISIBLE : View.GONE);
        tvStatus.setText(storageItems.size() + " storage volume(s)");
    }

    private void navigateTo(String path) {
        if (path == null || path.isEmpty()) {
            loadStorageList();
            return;
        }
        File dir = new File(path);
        if (!dir.exists() || !dir.isDirectory()) return;

        isHome = false;
        currentPath = path;
        loadFiles(path);
        updateBreadcrumbs(path);
    }

    private boolean navigateUp() {
        if (isHome) return false;

        // Check if we are at a storage root
        for (StorageUtils.StorageItem si : storageItems) {
            if (si.path.equals(currentPath)) {
                loadStorageList();
                return true;
            }
        }

        File parent = new File(currentPath).getParentFile();
        if (parent != null && parent.exists()) {
            currentPath = parent.getAbsolutePath();
            loadFiles(currentPath);
            updateBreadcrumbs(currentPath);
            return true;
        }

        loadStorageList();
        return true;
    }

    private void openMediaFile(String path) {
        Intent intent = new Intent(this, PlayerActivity.class);
        intent.putExtra(PlayerActivity.EXTRA_FILE_PATH, path);
        startActivity(intent);
    }

    // ──────────────────────────────────────────────────────────────────────
    // File loading
    // ──────────────────────────────────────────────────────────────────────

    private void loadFiles(String path) {
        File directory = new File(path);
        File[] children = directory.listFiles();
        List<FileItem> items = new ArrayList<>();

        if (children != null) {
            for (File f : children) {
                // Skip hidden files
                if (f.isHidden() || f.getName().startsWith(".")) continue;

                FileItem item = FileItem.fromFile(f);

                if (showMediaOnly) {
                    if (item.isDirectory) {
                        if (containsMediaFiles(f, 0)) {
                            items.add(item);
                        }
                    } else {
                        if (MimeTypeUtils.isMedia(MimeTypeUtils.getExtension(item.name))) {
                            items.add(item);
                        }
                    }
                } else {
                    items.add(item);
                }
            }
        }

        // Sort: directories first, then alphabetical
        Collections.sort(items, (a, b) -> {
            if (a.isDirectory != b.isDirectory) {
                return a.isDirectory ? -1 : 1;
            }
            return a.name.compareToIgnoreCase(b.name);
        });

        fileItems = items;
        listView.setAdapter(new FileAdapter());
        tvEmpty.setVisibility(items.isEmpty() ? View.VISIBLE : View.GONE);

        int dirCount = 0;
        int fileCount = 0;
        for (FileItem fi : items) {
            if (fi.isDirectory) dirCount++;
            else fileCount++;
        }
        tvStatus.setText(dirCount + " folder(s), " + fileCount + " file(s)");
    }

    /**
     * Recursively checks if a directory contains media files.
     *
     * @param directory the directory to check
     * @param depth     current recursion depth
     * @return true if media files found within MAX_SCAN_DEPTH levels
     */
    private boolean containsMediaFiles(File directory, int depth) {
        String key = directory.getAbsolutePath();
        Boolean cached = mediaFolderCache.get(key);
        if (cached != null) return cached;

        if (depth > MAX_SCAN_DEPTH) return true;

        try {
            File[] files = directory.listFiles();
            if (files == null) {
                mediaFolderCache.put(key, false);
                return false;
            }
            for (File f : files) {
                if (f.isHidden() || f.getName().startsWith(".")) continue;
                if (f.isDirectory()) {
                    if (containsMediaFiles(f, depth + 1)) {
                        mediaFolderCache.put(key, true);
                        return true;
                    }
                } else {
                    if (MimeTypeUtils.isMedia(MimeTypeUtils.getExtension(f.getName()))) {
                        mediaFolderCache.put(key, true);
                        return true;
                    }
                }
            }
        } catch (Exception e) {
            Log.w(TAG, "Cannot scan directory: " + key, e);
        }
        mediaFolderCache.put(key, false);
        return false;
    }

    // ──────────────────────────────────────────────────────────────────────
    // Breadcrumb navigation
    // ──────────────────────────────────────────────────────────────────────

    private void updateBreadcrumbs(String path) {
        breadcrumbs.clear();
        breadcrumbs.add(new BreadcrumbItem("Home", ""));

        // Find which storage root this path belongs to
        StorageUtils.StorageItem storage = null;
        for (StorageUtils.StorageItem si : storageItems) {
            if (path.startsWith(si.path)) {
                storage = si;
                break;
            }
        }

        if (storage != null) {
            breadcrumbs.add(new BreadcrumbItem(storage.name, storage.path));
            String relative = path.substring(storage.path.length());
            if (relative.startsWith("/")) relative = relative.substring(1);
            if (!relative.isEmpty()) {
                String buildPath = storage.path;
                for (String segment : relative.split("/")) {
                    if (!segment.isEmpty()) {
                        buildPath = buildPath + "/" + segment;
                        breadcrumbs.add(new BreadcrumbItem(segment, buildPath));
                    }
                }
            }
        }
        updateBreadcrumbsUI();
    }

    private void updateBreadcrumbsUI() {
        breadcrumbContainer.removeAllViews();
        for (int i = 0; i < breadcrumbs.size(); i++) {
            final BreadcrumbItem item = breadcrumbs.get(i);
            boolean isLast = (i == breadcrumbs.size() - 1);

            // Add separator arrow (except before first item)
            if (i > 0) {
                TextView arrow = new TextView(this);
                arrow.setText(" › ");
                arrow.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16);
                arrow.setTextColor(0xFFAAAAAA);
                arrow.setGravity(Gravity.CENTER_VERTICAL);
                breadcrumbContainer.addView(arrow);
            }

            TextView tv = new TextView(this);
            tv.setText(item.name);
            tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16);
            tv.setTextColor(isLast ? 0xFFFFFFFF : 0xFFBBDEFB);
            tv.setGravity(Gravity.CENTER_VERTICAL);
            int pad = dpToPx(4);
            tv.setPadding(pad, pad, pad, pad);

            if (!isLast) {
                tv.setOnClickListener(v -> {
                    if (item.path.isEmpty()) {
                        loadStorageList();
                    } else {
                        navigateTo(item.path);
                    }
                });
            }
            breadcrumbContainer.addView(tv);
        }

        // Scroll to end
        breadcrumbScroll.post(() ->
                breadcrumbScroll.fullScroll(HorizontalScrollView.FOCUS_RIGHT));
    }

    // ──────────────────────────────────────────────────────────────────────
    // Adapters
    // ──────────────────────────────────────────────────────────────────────

    /** Adapter for the storage volume list (home screen). */
    private class StorageAdapter extends BaseAdapter {
        @Override
        public int getCount() { return storageItems.size(); }

        @Override
        public StorageUtils.StorageItem getItem(int position) {
            return storageItems.get(position);
        }

        @Override
        public long getItemId(int position) { return position; }

        @Override
        public View getView(int position, View convertView, ViewGroup parent) {
            View view = convertView;
            if (view == null) {
                view = getLayoutInflater().inflate(R.layout.item_storage, parent, false);
            }
            StorageUtils.StorageItem item = getItem(position);

            TextView icon = view.findViewById(R.id.tvStorageIcon);
            TextView name = view.findViewById(R.id.tvStorageName);
            TextView space = view.findViewById(R.id.tvStorageSpace);
            ProgressBar pb = view.findViewById(R.id.pbStorageSpace);

            switch (item.type) {
                case INTERNAL: icon.setText("📱"); break;
                case SD_CARD:  icon.setText("💾"); break;
                case USB:      icon.setText("🔌"); break;
            }
            name.setText(item.name);

            long used = item.totalSpace - item.freeSpace;
            space.setText(StorageUtils.formatFileSize(used) + " / "
                    + StorageUtils.formatFileSize(item.totalSpace));
            if (item.totalSpace > 0) {
                pb.setMax(100);
                pb.setProgress((int) (used * 100 / item.totalSpace));
            }
            return view;
        }
    }

    /** Adapter for the file/directory list. */
    private class FileAdapter extends BaseAdapter {
        @Override
        public int getCount() { return fileItems.size(); }

        @Override
        public FileItem getItem(int position) { return fileItems.get(position); }

        @Override
        public long getItemId(int position) { return position; }

        @Override
        public View getView(int position, View convertView, ViewGroup parent) {
            View view = convertView;
            if (view == null) {
                view = getLayoutInflater().inflate(R.layout.item_file, parent, false);
            }
            FileItem item = getItem(position);

            TextView icon = view.findViewById(R.id.tvFileIcon);
            TextView name = view.findViewById(R.id.tvFileName);
            TextView info = view.findViewById(R.id.tvFileInfo);

            if (item.isDirectory) {
                icon.setText("📁");
                int mediaCount = countImmediateMedia(item.file);
                info.setText(mediaCount + " media file(s)");
            } else {
                MimeTypeUtils.MediaType mt =
                        MimeTypeUtils.getMediaType(item.name, false);
                switch (mt) {
                    case VIDEO: icon.setText("🎬"); break;
                    case AUDIO: icon.setText("🎵"); break;
                    default:    icon.setText("📄"); break;
                }
                info.setText(StorageUtils.formatFileSize(item.size) + "  ·  "
                        + StorageUtils.formatDate(item.lastModified));
            }
            name.setText(item.name);
            return view;
        }

        private int countImmediateMedia(File dir) {
            try {
                File[] files = dir.listFiles();
                if (files == null) return 0;
                int count = 0;
                for (File f : files) {
                    if (f.isHidden() || f.getName().startsWith(".")) continue;
                    if (f.isDirectory()) {
                        count++;
                    } else if (MimeTypeUtils.isMedia(MimeTypeUtils.getExtension(f.getName()))) {
                        count++;
                    }
                }
                return count;
            } catch (Exception e) {
                return 0;
            }
        }
    }

    // ──────────────────────────────────────────────────────────────────────
    // Helpers
    // ──────────────────────────────────────────────────────────────────────

    private int dpToPx(int dp) {
        return (int) TypedValue.applyDimension(
                TypedValue.COMPLEX_UNIT_DIP, dp, getResources().getDisplayMetrics());
    }
}
