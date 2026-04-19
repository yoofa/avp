package io.github.yoofa.avpdemo;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.DnsResolver;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkRequest;
import android.os.Build;
import android.util.Log;

import io.github.yoofa.avp.HttpConnection;
import io.github.yoofa.avp.HttpProvider;

import okhttp3.Dns;
import okhttp3.MediaType;
import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.Response;
import okhttp3.ResponseBody;

import java.io.BufferedInputStream;
import java.io.IOException;
import java.net.InetAddress;
import java.net.MalformedURLException;
import java.net.ProtocolException;
import java.net.UnknownHostException;
import java.net.URL;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

/** MediaHTTPConnection-style streaming HTTP provider backed by OkHttp. */
public final class OkHttpHttpProvider implements HttpProvider {
    private static final String TAG = "OkHttpHttpProvider";
    private final ConnectivityManager connectivityManager;
    private final OkHttpClient client;

    public OkHttpHttpProvider(Context context) {
        Context appContext = context.getApplicationContext();
        connectivityManager =
                (ConnectivityManager) appContext.getSystemService(Context.CONNECTIVITY_SERVICE);
        client =
                new OkHttpClient.Builder()
                        .followRedirects(true)
                        .followSslRedirects(true)
                        .retryOnConnectionFailure(true)
                        .build();
    }

    @Override
    public HttpConnection createConnection() {
        return new StreamingConnection(client, connectivityManager);
    }

    @Override
    public boolean supportsScheme(String scheme) {
        if (scheme == null) {
            return false;
        }
        String normalized = scheme.toLowerCase(Locale.US);
        return "http".equals(normalized) || "https".equals(normalized);
    }

    private static final class StreamingConnection implements HttpConnection {
        private static final int READ_BUFFER_SIZE = 32 * 1024;
        private static final int NETWORK_REQUEST_TIMEOUT_MS = 5_000;

        private final OkHttpClient baseClient;
        private final ConnectivityManager connectivityManager;
        private final Map<String, String> headers = new HashMap<>();
        private String requestUri = "";
        private String mimeType = "";
        private String resolvedUri = "";
        private long currentOffset = -1;
        private long totalSize = -1;
        private Response response;
        private ResponseBody responseBody;
        private BufferedInputStream inputStream;

        StreamingConnection(OkHttpClient baseClient, ConnectivityManager connectivityManager) {
            this.baseClient = baseClient;
            this.connectivityManager = connectivityManager;
        }

        @Override
        public synchronized boolean connect(String uri, String[] headerKeys, String[] headerValues) {
            disconnect();
            if (uri == null) {
                Log.w(TAG, "connect: null uri");
                return false;
            }

            try {
                new URL(uri);
            } catch (MalformedURLException e) {
                Log.w(TAG, "connect: invalid uri=" + uri + " exception=" + e);
                disconnect();
                return false;
            }

            requestUri = uri;
            resolvedUri = uri;
            headers.clear();
            int headerCount =
                    Math.min(
                            headerKeys != null ? headerKeys.length : 0,
                            headerValues != null ? headerValues.length : 0);
            for (int i = 0; i < headerCount; ++i) {
                String key = headerKeys[i];
                String value = headerValues[i];
                if (key == null || value == null) {
                    continue;
                }
                headers.put(key, value);
            }
            return true;
        }

        @Override
        public synchronized void disconnect() {
            teardownConnectionLocked();
            headers.clear();
            requestUri = "";
            mimeType = "";
            resolvedUri = "";
            totalSize = -1;
        }

        @Override
        public synchronized byte[] readAt(long offset, int size) {
            if (offset < 0 || size <= 0 || requestUri.isEmpty()) {
                return new byte[0];
            }

            try {
                if (offset != currentOffset || inputStream == null) {
                    seekToLocked(offset);
                }

                byte[] data = new byte[size];
                int bytesRead = inputStream.read(data, 0, size);
                if (bytesRead <= 0) {
                    return new byte[0];
                }

                currentOffset += bytesRead;
                if (bytesRead == data.length) {
                    return data;
                }

                byte[] truncated = new byte[bytesRead];
                System.arraycopy(data, 0, truncated, 0, bytesRead);
                return truncated;
            } catch (IOException e) {
                Log.w(
                        TAG,
                        "readAt failed: uri="
                                + requestUri
                                + " offset="
                                + offset
                                + " size="
                                + size
                                + " exception="
                                + e);
                teardownConnectionLocked();
                return null;
            }
        }

        @Override
        public synchronized long getSize() {
            if (requestUri.isEmpty()) {
                return -1;
            }
            if (response == null) {
                try {
                    seekToLocked(0);
                } catch (IOException e) {
                    Log.w(TAG, "getSize failed: uri=" + requestUri + " exception=" + e);
                    return -1;
                }
            }
            return totalSize;
        }

        @Override
        public synchronized String getMimeType() {
            if (requestUri.isEmpty()) {
                return "";
            }
            if (response == null) {
                try {
                    seekToLocked(0);
                } catch (IOException e) {
                    Log.w(TAG, "getMimeType failed: uri=" + requestUri + " exception=" + e);
                    return "application/octet-stream";
                }
            }
            return mimeType;
        }

        @Override
        public synchronized String getUri() {
            return resolvedUri;
        }

        private void teardownConnectionLocked() {
            currentOffset = -1;
            if (inputStream != null) {
                try {
                    inputStream.close();
                } catch (IOException ignored) {
                }
                inputStream = null;
            }
            if (responseBody != null) {
                responseBody.close();
                responseBody = null;
            }
            if (response != null) {
                response.close();
                response = null;
            }
        }

        private void seekToLocked(long offset) throws IOException {
            teardownConnectionLocked();

            Network network = getUsableNetwork();
            if (network != null && connectivityManager != null) {
                boolean bound = connectivityManager.bindProcessToNetwork(network);
                if (!bound) {
                    Log.w(TAG, "seekTo: failed to bind process to selected network");
                }
            }
            OkHttpClient client = buildClientForNetwork(network);

            Request.Builder requestBuilder = new Request.Builder().url(requestUri);
            for (Map.Entry<String, String> entry : headers.entrySet()) {
                requestBuilder.header(entry.getKey(), entry.getValue());
            }
            if (!headers.containsKey("Accept-Encoding")) {
                requestBuilder.header("Accept-Encoding", "identity");
            }
            if (offset > 0) {
                requestBuilder.header("Range", "bytes=" + offset + "-");
            }

            Response openedResponse = client.newCall(requestBuilder.build()).execute();
            ResponseBody openedBody = openedResponse.body();
            if (openedBody == null) {
                openedResponse.close();
                throw new IOException("empty response body");
            }

            int code = openedResponse.code();
            if (code != 200 && code != 206) {
                Log.w(
                        TAG,
                        "seekTo: unexpected HTTP status uri="
                                + requestUri
                                + " offset="
                                + offset
                                + " code="
                                + code
                                + " message="
                                + openedResponse.message());
                openedBody.close();
                openedResponse.close();
                throw new IOException("unexpected HTTP status " + code);
            }
            if (offset > 0 && code != 206) {
                openedBody.close();
                openedResponse.close();
                throw new ProtocolException("server ignored range request");
            }

            response = openedResponse;
            responseBody = openedBody;
            inputStream = new BufferedInputStream(openedBody.byteStream(), READ_BUFFER_SIZE);
            currentOffset = offset;
            resolvedUri = openedResponse.request().url().toString();

            MediaType contentType = openedBody.contentType();
            mimeType = contentType != null ? contentType.toString() : "";
            if (mimeType.isEmpty()) {
                String headerMimeType = openedResponse.header("Content-Type");
                mimeType = headerMimeType != null ? headerMimeType : "";
            }
            totalSize = extractTotalSize(openedResponse, openedBody, code);
        }

        private OkHttpClient buildClientForNetwork(Network network) {
            if (network == null) {
                return baseClient;
            }
            return baseClient.newBuilder()
                    .dns(hostname -> resolveHostname(network, hostname))
                    .build();
        }

        private List<InetAddress> resolveHostname(Network network, String hostname)
                throws UnknownHostException {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q && network != null) {
                try {
                    List<InetAddress> resolved = queryDnsResolver(network, hostname);
                    if (!resolved.isEmpty()) {
                        return resolved;
                    }
                } catch (UnknownHostException e) {
                    Log.w(TAG, "resolveHostname: DnsResolver fallback for " + hostname + " exception=" + e);
                }
            }
            return Dns.SYSTEM.lookup(hostname);
        }

        private List<InetAddress> queryDnsResolver(Network network, String hostname)
                throws UnknownHostException {
            AtomicReference<List<InetAddress>> result = new AtomicReference<>();
            AtomicReference<Exception> error = new AtomicReference<>();
            CountDownLatch latch = new CountDownLatch(1);

            DnsResolver.Callback<List<InetAddress>> callback =
                    new DnsResolver.Callback<List<InetAddress>>() {
                        @Override
                        public void onAnswer(List<InetAddress> answer, int rcode) {
                            if (answer != null && !answer.isEmpty()) {
                                result.set(new ArrayList<>(answer));
                            }
                            latch.countDown();
                        }

                        @Override
                        public void onError(DnsResolver.DnsException e) {
                            error.set(e);
                            latch.countDown();
                        }
                    };

            try {
                DnsResolver.getInstance()
                        .query(network, hostname, DnsResolver.FLAG_EMPTY, Runnable::run, null, callback);
                if (!latch.await(NETWORK_REQUEST_TIMEOUT_MS, TimeUnit.MILLISECONDS)) {
                    throw new UnknownHostException("DnsResolver timeout for " + hostname);
                }
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                UnknownHostException hostException =
                        new UnknownHostException("DnsResolver interrupted for " + hostname);
                hostException.initCause(e);
                throw hostException;
            } catch (RuntimeException e) {
                UnknownHostException hostException =
                        new UnknownHostException("DnsResolver failed for " + hostname);
                hostException.initCause(e);
                throw hostException;
            }

            if (result.get() != null) {
                return result.get();
            }

            if (error.get() != null) {
                UnknownHostException hostException =
                        new UnknownHostException(
                                "DnsResolver error for " + hostname + ": " + error.get());
                hostException.initCause(error.get());
                throw hostException;
            }

            return new ArrayList<>();
        }

        private Network getUsableNetwork() {
            if (connectivityManager == null) {
                return null;
            }

            Network activeNetwork = connectivityManager.getActiveNetwork();
            if (hasInternetCapability(activeNetwork)) {
                return activeNetwork;
            }

            Network[] networks = connectivityManager.getAllNetworks();
            if (networks != null) {
                for (Network network : networks) {
                    if (hasInternetCapability(network)) {
                        return network;
                    }
                }
            }

            return requestNetworkBlocking();
        }

        private boolean hasInternetCapability(Network network) {
            if (network == null || connectivityManager == null) {
                return false;
            }
            NetworkCapabilities capabilities = connectivityManager.getNetworkCapabilities(network);
            return capabilities != null
                    && capabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET);
        }

        private Network requestNetworkBlocking() {
            if (connectivityManager == null) {
                return null;
            }

            AtomicReference<Network> networkRef = new AtomicReference<>();
            CountDownLatch latch = new CountDownLatch(1);
            ConnectivityManager.NetworkCallback callback =
                    new ConnectivityManager.NetworkCallback() {
                        @Override
                        public void onAvailable(Network network) {
                            if (hasInternetCapability(network) && networkRef.compareAndSet(null, network)) {
                                latch.countDown();
                            }
                        }

                        @Override
                        public void onUnavailable() {
                            latch.countDown();
                        }
                    };

            try {
                NetworkRequest request =
                        new NetworkRequest.Builder()
                                .addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
                                .build();
                connectivityManager.requestNetwork(request, callback);
                latch.await(NETWORK_REQUEST_TIMEOUT_MS, TimeUnit.MILLISECONDS);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            } catch (Exception e) {
                Log.w(TAG, "requestNetworkBlocking failed: exception=" + e);
            } finally {
                try {
                    connectivityManager.unregisterNetworkCallback(callback);
                } catch (Exception ignored) {
                }
            }
            return networkRef.get();
        }

        private static long extractTotalSize(Response response, ResponseBody body, int code) {
            if (code == 206) {
                String contentRange = response.header("Content-Range");
                if (contentRange != null) {
                    int lastSlash = contentRange.lastIndexOf('/');
                    if (lastSlash >= 0 && lastSlash + 1 < contentRange.length()) {
                        String total = contentRange.substring(lastSlash + 1).trim();
                        if (!"*".equals(total)) {
                            try {
                                return Long.parseLong(total);
                            } catch (NumberFormatException ignored) {
                            }
                        }
                    }
                }
                return -1;
            }
            return body.contentLength();
        }
    }
}
