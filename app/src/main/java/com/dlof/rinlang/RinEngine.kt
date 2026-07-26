package com.dlof.rinlang

import android.content.Context

/**
 * Kotlin gateway into the native Rin language engine written in C++.
 * The C++ implementation lives under app/src/main/cpp and is compiled
 * into libRinengine.so via CMake / the Android NDK.
 *
 * منذ إضافة الحفظ/التثبيت الحقيقيَّين على القرص (save / installation / file / writeFile / readFile...
 * داخل لغة الحاويات)، يحتاج المحرّك C++ جذر مسار حقيقي تُبنى فوقه كل هذه العمليات. [init] يمرّر
 * مجلد التطبيق الخاص (filesDir) — وهو تخزين معزول لا يحتاج أذونات — ليُستخدم كجذر لكل ذلك.
 * استدعِ [init] مرة واحدة مبكراً (مثلاً في أول onCreate يُنفَّذ) قبل أي [runSource].
 */
object RinEngine {

    init {
        System.loadLibrary("rinengine")
    }

    @Volatile
    private var baseDir: String = ""

    /** يُستدعى مرة واحدة (عادة من MainActivity.onCreate) لتفعيل تخزين حقيقي فعلي لـ save/installation/file. */
    fun init(context: Context) {
        baseDir = context.filesDir.absolutePath
    }

    /**
     * نفس [init] لكن مع تجاوز الجذر بمسار مشروع محدد (انظر [ProjectManager])، بحيث تعمل
     * save/installation/file لهذا المشروع فقط داخل مجلده الخاص بدل filesDir العام للتطبيق.
     */
    fun init(context: Context, projectBasePath: String) {
        baseDir = projectBasePath
    }

    /**
     * الجذر الحقيقي على القرص الذي تُبنى فوقه كل مسارات save/installation/file النسبية
     * (نفس المسار الممرَّر لـ runSourceNative). يُستخدم لتحديد موقع الملفات الفعلية
     * التي أنتجها آخر تشغيل حتى يمكن تنزيلها إلى مجلد Downloads العام بالجهاز.
     */
    fun currentBaseDir(): String = baseDir

    /** Lexes, parses and interprets [source]; returns everything the program printed. */
    fun runSource(source: String): String = runSourceNative(source, baseDir)

    private external fun runSourceNative(source: String, baseDir: String): String

    /** Returns a human readable version string for the native engine. */
    external fun engineVersion(): String
}
