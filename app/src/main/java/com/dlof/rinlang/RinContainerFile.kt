package com.dlof.rinlang

import java.io.File

/**
 * "ملف حاوية الويب" (Web Container File): ملف .rin يُعرَّف بـ**توقيع في أوله** لا باسمه.
 *
 * أي مستخدم يستطيع تسمية ملف أو حاوية `container` (ولغة Rin نفسها فيها `@container=Name`)، لذا الاسم
 * `container.rin` وحده لا يدل على شيء. الهوية الحقيقية هي ترويسة في أول سطر غير فارغ:
 *
 *     //! rin:container web
 *     //! rin:container web persist=count,userName      (حفظ هذه المتغيرات بين التشغيلات)
 *     //! rin:container web persist=*                   (حفظ كل المتغيرات العامة)
 *
 * السطر تعليق عادي في Rin (`//`) فلا يتأثر المحرّك به. التطبيق فقط يقرؤه ليعرف أن الملف منطق صفحة
 * ويب: أيقونة خاصة، وRun عليه يفتح index.html، واختياره تلقائياً ملفَ منطق للصفحة، وحفظ الحالة.
 */
object RinContainerFile {

    /** السطر الذي يُكتب أول القالب. */
    const val SIGNATURE = "//! rin:container web"
    const val KIND_WEB = "web"

    /**
     * @param persist أسماء المتغيرات المحفوظة (فارغة إن لم يُطلب حفظ).
     * @param persistAll true عند persist=* (كل المتغيرات العامة عدا الثوابت المدمجة).
     */
    data class Header(val kind: String, val persist: Set<String>, val persistAll: Boolean) {
        val persists: Boolean get() = persistAll || persist.isNotEmpty()
    }

    private val SIGNATURE_REGEX = Regex("""^//!\s*rin:container\s+([A-Za-z0-9_-]+)(.*)$""")
    private val PERSIST_REGEX = Regex("""persist\s*=\s*([^\s]+)""")

    /** المتغيرات العامة المدمجة في المحرّك؛ لا تُحفَظ عند persist=*. */
    val BUILTIN_GLOBALS: Set<String> = setOf("PI", "E", "PHI", "TAU")

    /** يقرأ الترويسة من أول سطر غير فارغ في [text]، أو null إن لم يكن ملف حاوية ويب. */
    fun parseHeader(text: String): Header? {
        val firstLine = text.removePrefix("﻿").lineSequence().map { it.trim() }.firstOrNull { it.isNotEmpty() } ?: return null
        val m = SIGNATURE_REGEX.matchEntire(firstLine) ?: return null
        val kind = m.groupValues[1].lowercase()
        if (kind != KIND_WEB) return null
        val spec = PERSIST_REGEX.find(m.groupValues[2])?.groupValues?.get(1).orEmpty()
        val all = spec == "*"
        val names = if (all) emptySet() else spec.split(',').map { it.trim() }.filter { it.isNotEmpty() }.toSet()
        return Header(kind, names, all)
    }

    /** يقرأ ترويسة [file] (أول 1024 بايت فقط)، أو null. */
    fun readHeader(file: File): Header? {
        if (!file.isFile || !file.name.endsWith(".rin", ignoreCase = true)) return null
        return try {
            val buf = ByteArray(1024)
            val n = file.inputStream().use { it.read(buf) }
            if (n <= 0) null else parseHeader(String(buf, 0, n, Charsets.UTF_8))
        } catch (t: Throwable) {
            null
        }
    }

    fun isWebContainer(file: File): Boolean = readHeader(file) != null

    /**
     * ملف حاوية الويب الموقَّع في جذر [projectDir]: يفضَّل container.rin إن كان موقَّعاً، وإلا أقدم
     * ملف .rin موقَّع بالاسم أبجدياً (ترتيب ثابت). null إن لم يوجد.
     */
    fun findIn(projectDir: File): File? {
        val candidates = projectDir.listFiles { f -> f.isFile && f.name.endsWith(".rin", ignoreCase = true) }
            ?.filter { isWebContainer(it) }
            ?.sortedBy { it.name.lowercase() }
            ?: return null
        return candidates.firstOrNull { it.name.equals("container.rin", ignoreCase = true) } ?: candidates.firstOrNull()
    }
}
