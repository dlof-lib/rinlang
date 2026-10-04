# Keep native method signatures used by JNI (RinEngine.kt <-> jni_bridge.cpp)
-keepclasseswithmembernames class * {
    native <methods>;
}
-keep class com.dlof.rinlang.RinEngine { *; }

# ---- R8 (تقليص الحجم، يُفعَّل في release عبر rin.minify) ----
# نُبقي كل كلاسات التطبيق كما هي: كثير منها يُملأ بالانعكاس (نماذج Firebase، JSON، View من XML، JNI)،
# والتقليص الفعلي يطال المكتبات الخارجية (Firebase/Material/AndroidX/zxing/apksig) وهي الجزء الأكبر من الـ dex.
-keep class com.dlof.rinlang.** { *; }

# تحذيرات مكتبات اختيارية غير موجودة على أندرويد (لا تؤثر وقت التشغيل).
-dontwarn javax.annotation.**
-dontwarn org.checkerframework.**
-dontwarn com.google.errorprone.annotations.**
-dontwarn sun.security.**
-dontwarn javax.naming.**
