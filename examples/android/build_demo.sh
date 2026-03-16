#!/bin/bash
# Build script for AVP Android Demo App
# Usage: ./build_demo.sh [debug|release]
#
# Prerequisites:
#   1. Build libavp_android.so first:
#      ninja -C ../../out/Debug_arm64 sdk/android:libavp
#
#   2. Set ANDROID_HOME environment variable

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
AVP_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_TYPE="${1:-debug}"

echo "=== AVP Demo Build Script ==="
echo "AVP root: $AVP_ROOT"
echo "Build type: $BUILD_TYPE"

# Find the .so file
SO_DIR="$AVP_ROOT/out/Debug_arm64"
SO_FILE="$SO_DIR/libavp_android.so"
UNSTRIPPED_SO="$SO_DIR/lib.unstripped/libavp_android.so"

if [ ! -f "$SO_FILE" ] && [ ! -f "$UNSTRIPPED_SO" ]; then
    echo "Error: libavp_android.so not found."
    echo "Build it first with:"
    echo "  ninja -C $SO_DIR sdk/android:libavp"
    exit 1
fi

# Copy .so to jniLibs (prefer unstripped for debug, stripped for release)
JNILIBS_DIR="$SCRIPT_DIR/app/src/main/jniLibs/arm64-v8a"
mkdir -p "$JNILIBS_DIR"

if [ "$BUILD_TYPE" = "release" ] && [ -f "$SO_FILE" ]; then
    echo "Copying stripped .so..."
    cp "$SO_FILE" "$JNILIBS_DIR/"
elif [ -f "$UNSTRIPPED_SO" ]; then
    echo "Copying unstripped .so for debug..."
    cp "$UNSTRIPPED_SO" "$JNILIBS_DIR/"
else
    cp "$SO_FILE" "$JNILIBS_DIR/"
fi

echo "Copied to: $JNILIBS_DIR/libavp_android.so"
ls -lh "$JNILIBS_DIR/libavp_android.so"

# Check for jni_zero.jar
JNI_ZERO_JAR="$AVP_ROOT/third_party/jni_zero/jni_zero.jar"
if [ ! -f "$JNI_ZERO_JAR" ]; then
    echo "Warning: jni_zero.jar not found at $JNI_ZERO_JAR"
    echo "Java compilation may fail for @CalledByNative annotations."
fi

# Build APK
echo ""
echo "=== Building APK ==="
cd "$SCRIPT_DIR"

if [ "$BUILD_TYPE" = "release" ]; then
    ./gradlew assembleRelease
    APK_PATH="app/build/outputs/apk/release/app-release-unsigned.apk"
else
    ./gradlew assembleDebug
    APK_PATH="app/build/outputs/apk/debug/app-debug.apk"
fi

if [ -f "$APK_PATH" ]; then
    echo ""
    echo "=== Build Successful ==="
    echo "APK: $SCRIPT_DIR/$APK_PATH"
    ls -lh "$APK_PATH"

    echo ""
    echo "Install with:"
    echo "  adb install -r $SCRIPT_DIR/$APK_PATH"
else
    echo "Error: APK not found at expected path"
    exit 1
fi
