# Keep JNI native methods
-keepclasseswithmembernames class * {
    native <methods>;
}

# Keep AVP SDK classes called from native code
-keep class io.github.yoofa.avp.** { *; }
-keep class io.github.yoofa.media.** { *; }
-keep class io.github.yoofa.** { *; }

# Keep jni_zero annotations
-keep @interface org.jni_zero.**
-keepclassmembers class * {
    @org.jni_zero.CalledByNative *;
}
