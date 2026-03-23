package io.github.yoofa.avpdemo;

import java.io.File;

/**
 * Data model representing a file or directory in the file browser.
 */
public class FileItem {
    public final File file;
    public final String name;
    public final String path;
    public final boolean isDirectory;
    public final long size;
    public final long lastModified;

    private FileItem(File file) {
        this.file = file;
        this.name = file.getName();
        this.path = file.getAbsolutePath();
        this.isDirectory = file.isDirectory();
        this.size = file.isFile() ? file.length() : 0;
        this.lastModified = file.lastModified();
    }

    /**
     * Creates a FileItem from a java.io.File.
     *
     * @param file the source file
     * @return a new FileItem instance
     */
    public static FileItem fromFile(File file) {
        return new FileItem(file);
    }
}
