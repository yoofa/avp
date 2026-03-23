package io.github.yoofa.avpdemo;

import android.content.Context;
import android.os.Build;
import android.os.Environment;
import android.os.storage.StorageManager;
import android.os.storage.StorageVolume;

import java.io.File;
import java.text.DecimalFormat;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Locale;

/**
 * Utility class for detecting storage volumes and formatting file metadata.
 */
public final class StorageUtils {

    /** Storage device types. */
    public enum StorageType {
        INTERNAL,
        USB,
        SD_CARD
    }

    /** Represents a mounted storage volume. */
    public static class StorageItem {
        public final String name;
        public final String path;
        public final StorageType type;
        public final long totalSpace;
        public final long freeSpace;

        public StorageItem(String name, String path, StorageType type,
                           long totalSpace, long freeSpace) {
            this.name = name;
            this.path = path;
            this.type = type;
            this.totalSpace = totalSpace;
            this.freeSpace = freeSpace;
        }
    }

    private StorageUtils() {}

    /**
     * Returns a list of all mounted storage volumes.
     *
     * @param context the application context
     * @return list of available storage items
     */
    public static List<StorageItem> getStorageList(Context context) {
        List<StorageItem> storageList = new ArrayList<>();
        StorageManager sm = (StorageManager) context.getSystemService(Context.STORAGE_SERVICE);
        if (sm == null) {
            return addFallbackInternal(storageList);
        }

        List<StorageVolume> volumes = sm.getStorageVolumes();
        for (StorageVolume volume : volumes) {
            String state = volume.getState();
            if (!Environment.MEDIA_MOUNTED.equals(state)
                    && !Environment.MEDIA_MOUNTED_READ_ONLY.equals(state)) {
                continue;
            }

            String path = getVolumePath(volume);
            if (path == null || !new File(path).exists()) continue;

            File file = new File(path);
            String name = volume.getDescription(context);
            if (name == null) name = "Storage";
            StorageType type = determineStorageType(volume);

            storageList.add(new StorageItem(
                    name, path, type, file.getTotalSpace(), file.getFreeSpace()));
        }

        if (storageList.isEmpty()) {
            return addFallbackInternal(storageList);
        }
        return storageList;
    }

    private static String getVolumePath(StorageVolume volume) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            File dir = volume.getDirectory();
            return dir != null ? dir.getAbsolutePath() : null;
        }
        // Fallback for API 26-29: use reflection
        try {
            java.lang.reflect.Method getPath =
                    StorageVolume.class.getMethod("getPath");
            return (String) getPath.invoke(volume);
        } catch (Exception e) {
            return null;
        }
    }

    private static StorageType determineStorageType(StorageVolume volume) {
        if (volume.isPrimary()) return StorageType.INTERNAL;
        if (volume.isRemovable()) {
            String desc = volume.getDescription(null);
            if (desc != null) {
                String lower = desc.toLowerCase();
                if (lower.contains("sd")) return StorageType.SD_CARD;
            }
            return StorageType.USB;
        }
        return StorageType.INTERNAL;
    }

    private static List<StorageItem> addFallbackInternal(List<StorageItem> list) {
        File internal = Environment.getExternalStorageDirectory();
        list.add(new StorageItem(
                "Internal Storage",
                internal.getAbsolutePath(),
                StorageType.INTERNAL,
                internal.getTotalSpace(),
                internal.getFreeSpace()));
        return list;
    }

    /**
     * Formats a byte count into a human-readable string.
     *
     * @param bytes the size in bytes
     * @return formatted string (e.g. "1.5 GB")
     */
    public static String formatFileSize(long bytes) {
        if (bytes <= 0) return "0 B";
        String[] units = {"B", "KB", "MB", "GB", "TB"};
        int group = (int) (Math.log10((double) bytes) / Math.log10(1024));
        group = Math.min(group, units.length - 1);
        double value = bytes / Math.pow(1024, group);
        return new DecimalFormat("#,##0.##").format(value) + " " + units[group];
    }

    /**
     * Formats a timestamp into a readable date string.
     *
     * @param timestamp the Unix timestamp in milliseconds
     * @return formatted date string
     */
    public static String formatDate(long timestamp) {
        SimpleDateFormat sdf = new SimpleDateFormat("yyyy-MM-dd HH:mm", Locale.getDefault());
        return sdf.format(new Date(timestamp));
    }
}
