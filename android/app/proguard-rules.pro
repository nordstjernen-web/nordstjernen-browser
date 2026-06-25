# Keep JNI entry points reachable from native code.
-keepclasseswithmembernames class org.nordstjernen.WebBrowser.NativeBrowser {
    native <methods>;
}
