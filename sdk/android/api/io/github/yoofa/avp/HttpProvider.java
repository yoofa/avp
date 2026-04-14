/*
 * Copyright (C) 2026 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

package io.github.yoofa.avp;

/** Java-side HTTP provider factory used by the native HTTP bridge. */
public interface HttpProvider {
    HttpConnection createConnection();

    boolean supportsScheme(String scheme);
}
