package com.dlof.rinlang

/** منطق خالص (بلا أندرويد) لحفظ المشاريع في مجلد على الهاتف — يُختبر في tools/kotlin-tests. */
object ProjectSavePlan {
    /** اسم مجلد غير مستخدم: base ثم base-2 ثم base-3… (المقارنة بلا حساسية لحالة الأحرف). */
    fun uniqueName(existing: Collection<String>, base: String): String {
        val used = existing.map { it.lowercase() }.toSet()
        if (base.lowercase() !in used) return base
        var i = 2
        while ("$base-$i".lowercase() in used) i++
        return "$base-$i"
    }

    /** المسارات المخفية الداخلية (.rin_state، .import_*…) لا تُنسخ؛ و .gitignore يُنسخ. */
    fun shouldSkip(relPath: String): Boolean =
        relPath.split('/').any { it.startsWith(".") && it != ".gitignore" }

    /** هل يلزم نسخ الملف؟ مفقود/حجم مختلف/المصدر أحدث بأكثر من ثانيتين (دقة الطوابع تختلف بين المزوّدات). */
    fun needsCopy(srcSize: Long, srcModified: Long, dstSize: Long, dstModified: Long): Boolean =
        dstSize < 0 || srcSize != dstSize || srcModified > dstModified + 2000
}
