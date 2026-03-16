# AVP Android Demo App

A minimal Android demo application that demonstrates the AVP player SDK.

## Features

- Simple file path input for media playback
- Fullscreen video player with SurfaceView rendering
- Play/pause controls with seek bar
- Auto-hiding control overlay
- Supports opening video files from other apps

## Prerequisites

1. Build the native library:
   ```bash
   # From the avp root directory
   gn gen out/Debug_arm64 --args='target_os="android" target_cpu="arm64"'
   ninja -C out/Debug_arm64 sdk/android:libavp
   ```

2. Set `ANDROID_HOME` environment variable pointing to your Android SDK.

## Build

```bash
# Quick build (copies .so + builds APK)
./build_demo.sh

# Or manually:
# 1. Copy the native library
cp ../../out/Debug_arm64/lib.unstripped/libavp_android.so \
   app/src/main/jniLibs/arm64-v8a/

# 2. Build the APK
./gradlew assembleDebug
```

## Install & Run

```bash
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n io.github.yoofa.avpdemo/.MainActivity
```

## Architecture

```
examples/android/
├── build_demo.sh          # Build helper script
├── app/
│   ├── build.gradle       # Includes AVP SDK Java sources directly
│   └── src/main/
│       ├── AndroidManifest.xml
│       ├── java/.../avpdemo/
│       │   ├── MainActivity.kt    # File path input
│       │   └── PlayerActivity.kt  # Fullscreen player
│       ├── jniLibs/arm64-v8a/     # libavp_android.so (copied by build_demo.sh)
│       └── res/                   # Layouts, themes, colors
```

The app includes AVP SDK Java sources (`sdk/android/api`, `base/android/java`,
`media/android/java`) directly via `srcDirs` in build.gradle, rather than as
a separate library module. The native `libavp_android.so` is loaded at runtime
via `System.loadLibrary("avp_android")`.
