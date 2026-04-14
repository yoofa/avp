/*
 * Copyright (C) 2026 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

package io.github.yoofa.avp;

/** Java-side HTTP connection used by the native HTTP bridge. */
public interface HttpConnection {
    boolean connect(String uri, String[] headerKeys, String[] headerValues);

    void disconnect();

    byte[] readAt(long offset, int size);

    long getSize();

    String getMimeType();

    String getUri();
}
