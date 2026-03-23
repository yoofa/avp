package io.github.yoofa.avpdemo;

import java.util.Arrays;
import java.util.HashSet;
import java.util.Set;

/**
 * Utility class for identifying media file types by extension.
 * Provides comprehensive audio/video format recognition for the file browser.
 */
public final class MimeTypeUtils {

    /** Audio file extensions. */
    private static final Set<String> AUDIO_EXTENSIONS = new HashSet<>(Arrays.asList(
            "mp3", "m4a", "m4b", "m4p", "m4r",
            "aac", "adt", "adts",
            "wav", "wave",
            "flac",
            "ogg", "oga", "opus", "spx",
            "wma",
            "aif", "aiff", "aifc",
            "mid", "midi",
            "amr", "awb",
            "3ga",
            "ac3", "dts", "dtshd",
            "mka",
            "ra", "ram",
            "au", "snd",
            "ape", "wv", "tta", "tak",
            "mpa", "mpga", "mp1", "mp2"
    ));

    /** Video file extensions. */
    private static final Set<String> VIDEO_EXTENSIONS = new HashSet<>(Arrays.asList(
            "mp4", "m4v", "m4s", "mp4v",
            "mkv", "webm",
            "avi",
            "mov", "qt",
            "wmv",
            "flv", "f4v",
            "3gp", "3gpp", "3g2", "3gpp2",
            "mpeg", "mpg", "mpe",
            "m1v", "m2v",
            "m2t", "m2ts", "mts", "ts",
            "vob",
            "ogv",
            "asf",
            "rm", "rmvb",
            "divx", "xvid"
    ));

    private MimeTypeUtils() {}

    /**
     * Gets the lowercase file extension from a filename.
     *
     * @param fileName the file name
     * @return the extension without the dot, or empty string if none
     */
    public static String getExtension(String fileName) {
        if (fileName == null) return "";
        int dotIndex = fileName.lastIndexOf('.');
        if (dotIndex < 0 || dotIndex == fileName.length() - 1) return "";
        return fileName.substring(dotIndex + 1).toLowerCase();
    }

    /**
     * Checks if the extension belongs to an audio file.
     *
     * @param extension the file extension (without dot)
     * @return true if audio
     */
    public static boolean isAudio(String extension) {
        return AUDIO_EXTENSIONS.contains(extension.toLowerCase());
    }

    /**
     * Checks if the extension belongs to a video file.
     *
     * @param extension the file extension (without dot)
     * @return true if video
     */
    public static boolean isVideo(String extension) {
        return VIDEO_EXTENSIONS.contains(extension.toLowerCase());
    }

    /**
     * Checks if the extension belongs to a media file (audio or video).
     *
     * @param extension the file extension (without dot)
     * @return true if media
     */
    public static boolean isMedia(String extension) {
        String ext = extension.toLowerCase();
        return AUDIO_EXTENSIONS.contains(ext) || VIDEO_EXTENSIONS.contains(ext);
    }

    /**
     * Media type classification.
     */
    public enum MediaType {
        AUDIO,
        VIDEO,
        DIRECTORY,
        OTHER
    }

    /**
     * Determines the media type of a file.
     *
     * @param fileName    the file name
     * @param isDirectory whether the file is a directory
     * @return the media type
     */
    public static MediaType getMediaType(String fileName, boolean isDirectory) {
        if (isDirectory) return MediaType.DIRECTORY;
        String ext = getExtension(fileName);
        if (AUDIO_EXTENSIONS.contains(ext)) return MediaType.AUDIO;
        if (VIDEO_EXTENSIONS.contains(ext)) return MediaType.VIDEO;
        return MediaType.OTHER;
    }
}
