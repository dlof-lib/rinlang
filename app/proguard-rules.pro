# Keep native method signatures used by JNI (RinEngine.kt <-> jni_bridge.cpp)
-keepclasseswithmembernames class * {
    native <methods>;
}
-keep class com.dlof.rinlang.RinEngine { *; }
