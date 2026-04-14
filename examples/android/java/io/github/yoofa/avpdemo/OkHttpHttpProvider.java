package io.github.yoofa.avpdemo;

import io.github.yoofa.avp.HttpConnection;
import io.github.yoofa.avp.HttpProvider;

import okhttp3.MediaType;
import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.Response;
import okhttp3.ResponseBody;

import java.io.IOException;
import java.util.Arrays;
import java.util.Locale;

/** OkHttp-backed HTTP provider for the example app's native HLS/HTTP bridge. */
public final class OkHttpHttpProvider implements HttpProvider {
    private final OkHttpClient client;

    public OkHttpHttpProvider() {
        this(new OkHttpClient.Builder().followRedirects(true).followSslRedirects(true).build());
    }

    OkHttpHttpProvider(OkHttpClient client) {
        this.client = client;
    }

    @Override
    public HttpConnection createConnection() {
        return new OkHttpHttpConnection(client);
    }

    @Override
    public boolean supportsScheme(String scheme) {
        if (scheme == null) {
            return false;
        }
        String normalized = scheme.toLowerCase(Locale.US);
        return "http".equals(normalized) || "https".equals(normalized);
    }

    private static final class OkHttpHttpConnection implements HttpConnection {
        private final OkHttpClient client;
        private byte[] responseBytes = new byte[0];
        private String mimeType = "";
        private String resolvedUri = "";

        OkHttpHttpConnection(OkHttpClient client) {
            this.client = client;
        }

        @Override
        public boolean connect(String uri, String[] headerKeys, String[] headerValues) {
            disconnect();

            Request.Builder requestBuilder = new Request.Builder().url(uri);
            int headerCount = Math.min(headerKeys.length, headerValues.length);
            for (int i = 0; i < headerCount; ++i) {
                String key = headerKeys[i];
                String value = headerValues[i];
                if (key == null || value == null) {
                    continue;
                }
                requestBuilder.addHeader(key, value);
            }

            try (Response response = client.newCall(requestBuilder.build()).execute()) {
                if (!response.isSuccessful()) {
                    return false;
                }
                ResponseBody body = response.body();
                if (body == null) {
                    return false;
                }
                responseBytes = body.bytes();
                MediaType contentType = body.contentType();
                mimeType = contentType != null ? contentType.toString() : "";
                resolvedUri = response.request().url().toString();
                return true;
            } catch (IOException e) {
                disconnect();
                return false;
            }
        }

        @Override
        public void disconnect() {
            responseBytes = new byte[0];
            mimeType = "";
            resolvedUri = "";
        }

        @Override
        public byte[] readAt(long offset, int size) {
            if (offset < 0 || offset >= responseBytes.length || size <= 0) {
                return new byte[0];
            }
            int start = (int) offset;
            int end = Math.min(responseBytes.length, start + size);
            return Arrays.copyOfRange(responseBytes, start, end);
        }

        @Override
        public long getSize() {
            return responseBytes.length;
        }

        @Override
        public String getMimeType() {
            return mimeType;
        }

        @Override
        public String getUri() {
            return resolvedUri;
        }
    }
}
