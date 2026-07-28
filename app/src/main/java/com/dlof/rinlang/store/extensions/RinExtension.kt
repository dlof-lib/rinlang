package com.dlof.rinlang.store.extensions

/**
 * أنواع الإضافات المدعومة في "Rin Extensions Marketplace". يُستخدم للتصنيف والفلترة في
 * واجهة المتجر، ولتحديد "معاينة الوصول" المعروضة للمستخدم قبل التثبيت في [ExtensionSecurity].
 */
enum class ExtensionType(val id: String) {
    EXTENSION("extension"),      // إضافات عامة للمحرر (أوامر، تحسينات، تكاملات)
    LIBRARY("library"),          // مكتبات لغة Rin القابلة للاستيراد بـ @import
    THEME("theme"),              // سمات ألوان/تنسيق للمحرر
    UI_COMPONENT("ui_component"),// عناصر واجهة إضافية (لوحات، أزرار، شرائط أدوات)
    DEBUG_TOOL("debug_tool"),    // أدوات تتبّع/تصحيح إضافية
    AI_TOOL("ai_tool"),          // أدوات مبنية على نماذج ذكاء اصطناعي
    TEMPLATE("template");        // قوالب مشاريع جاهزة

    companion object {
        fun fromId(id: String): ExtensionType =
            values().find { it.id.equals(id, ignoreCase = true) } ?: EXTENSION
    }
}

/** لقطة شاشة مرفقة بالإضافة، مخزَّنة بنفس أسلوب [RinPackage.base64Data]: base64 داخل حقل نصي. */
data class ExtensionScreenshot(
    val base64Data: String = "",
    val caption: String = ""
)

/** سطر واحد في سجل تحديثات الإضافة (Changelog). */
data class ExtensionChangelogEntry(
    val version: String = "",
    val date: Long = 0L,
    val notes: String = ""
)

/**
 * إضافة منشورة في "Rin Extensions Marketplace". تُخزَّن العناصر تحت /extensions/{extensionId}
 * في Realtime Database، بنفس فلسفة [com.dlof.rinlang.store.RinPackage]: قراءة عامة، وكتابة
 * مقصورة على المطوّر صاحب الإضافة فقط (راجع firebase/database.rules.json).
 *
 * [base64Data] هو أرشيف zip كامل لملفات محتوى الإضافة مُرمَّز base64 داخل حقل نصي واحد، بلا
 * Firebase Storage مدفوع — تماماً كما تفعل [RinPackage]. عند التصدير عبر [RinexPackager] يُغلَّف
 * هذا المحتوى مع وصف الإضافة (extension.rinext) داخل ملف واحد مستقل بامتداد **.rinex** — تنسيق
 * Rin Extensions الخاص، القابل للمشاركة والتثبيت بلا اتصال إنترنت.
 *
 * constructor بلا معاملات مطلوب لإعادة البناء التلقائي عبر DataSnapshot.getValue(...).
 */
data class RinExtension(
    val id: String = "",
    val name: String = "",
    val version: String = "1.0.0",
    val developer: String = "",
    val developerUid: String = "",
    val description: String = "",
    val type: String = ExtensionType.EXTENSION.id,
    /** أذونات مطلوبة (معرّفات نصية)، راجع [ExtensionPermissions.CATALOG] لوصفها بالعربية. */
    val permissions: List<String> = emptyList(),
    val languages: List<String> = emptyList(),
    val screenshots: List<ExtensionScreenshot> = emptyList(),
    val changelog: List<ExtensionChangelogEntry> = emptyList(),
    val releaseDate: Long = 0L,
    val sizeBytes: Long = 0L,
    val fileName: String = "extension.rinex",
    val base64Data: String = "",
    /**
     * بصمة تكامل (SHA-256) لمحتوى [base64Data]، تُحسَب عند النشر وتُعرَض للمستخدم كـ"توقيع رقمي"
     * قبل التثبيت — راجع [ExtensionSecurity.computeSignature] للتفاصيل وحدود هذا الأسلوب.
     */
    val signature: String = "",
    val downloadCount: Long = 0L,
    val ratingSum: Long = 0L,
    val ratingCount: Long = 0L,
    val reportCount: Long = 0L,
    val likeCount: Long = 0L
) : java.io.Serializable {

    val averageRating: Double
        get() = if (ratingCount <= 0L) 0.0 else ratingSum.toDouble() / ratingCount.toDouble()

    val extensionType: ExtensionType
        get() = ExtensionType.fromId(type)
}
