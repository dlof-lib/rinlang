package com.dlof.rinlang.store.extensions

import java.security.MessageDigest

/** وصف إذن واحد بالعربية، يُعرَض للمستخدم في شاشة الأمان قبل التثبيت. */
data class PermissionInfo(
    val id: String,
    val label: String,
    val description: String
)

/**
 * نظام الأمان المعروض قبل تثبيت أي إضافة: يترجم الأذونات الخام (["project.write", ...]) إلى
 * وصف عربي مفهوم، ويحسب/يتحقق من "التوقيع الرقمي" لمحتوى الإضافة، ويبني معاينة الملفات
 * التي ستصل إليها الإضافة حسب نوعها.
 */
object ExtensionPermissions {

    /** كتالوج الأذونات المعروفة. أي معرّف غير موجود هنا يُعرَض بنصّه الخام مع وصف عام. */
    val CATALOG: List<PermissionInfo> = listOf(
        PermissionInfo("project.read", "قراءة ملفات المشروع",
            "تستطيع الإضافة قراءة كل ملفات المشروع الحالي (main.rin وباقي ملفات .rin)."),
        PermissionInfo("project.write", "تعديل ملفات المشروع",
            "تستطيع الإضافة إنشاء أو تعديل أو حذف ملفات داخل المشروع الحالي."),
        PermissionInfo("editor.access", "الوصول إلى المحرر",
            "تستطيع الإضافة التفاعل مباشرة مع محرر الكود (إدراج نصوص، تظليل، اقتراحات)."),
        PermissionInfo("library.read", "قراءة المكتبات",
            "تستطيع الإضافة قراءة مكتبات lib/*.og.rin الخاصة بالمشروع."),
        PermissionInfo("library.write", "تعديل المكتبات",
            "تستطيع الإضافة إنشاء أو تعديل مكتبات داخل مجلد lib/ الخاص بالمشروع."),
        PermissionInfo("console.access", "قراءة ناتج التشغيل",
            "تستطيع الإضافة قراءة ناتج تشغيل الكود (Console) وسجلات التنفيذ."),
        PermissionInfo("network.access", "الوصول إلى الإنترنت",
            "تستطيع الإضافة إجراء اتصالات شبكة خارجية (مثلاً استدعاء واجهات AI)."),
        PermissionInfo("filesystem.read", "قراءة ملفات الجهاز",
            "تستطيع الإضافة قراءة ملفات خارج مشاريع Rin على الجهاز (بعد إذن النظام)."),
        PermissionInfo("filesystem.write", "الكتابة على ملفات الجهاز",
            "تستطيع الإضافة إنشاء أو تعديل ملفات خارج مشاريع Rin على الجهاز."),
        PermissionInfo("settings.write", "تعديل إعدادات التطبيق",
            "تستطيع الإضافة تغيير إعدادات عامة في تطبيق Rin (حجم الخط، السمة...).")
    )

    private val byId: Map<String, PermissionInfo> = CATALOG.associateBy { it.id }

    /** يترجم معرّف إذن خام إلى وصف عربي، أو وصف عام إن لم يكن معروفاً في الكتالوج. */
    fun describe(permissionId: String): PermissionInfo =
        byId[permissionId] ?: PermissionInfo(
            id = permissionId,
            label = permissionId,
            description = "إذن غير موثَّق مسبقاً من فريق Rin — تحقّق من المطوّر قبل المتابعة."
        )

    fun describeAll(permissionIds: List<String>): List<PermissionInfo> = permissionIds.map(::describe)

    /**
     * وصف عام لمناطق الوصول المتوقّعة حسب نوع الإضافة، لعرضها ضمن "الملفات التي ستصل إليها"
     * في شاشة الأمان — معاينة توضيحية عامة، وليست قائمة مسارات حرفية.
     */
    fun accessScopeDescription(type: ExtensionType): String = when (type) {
        ExtensionType.EXTENSION -> "مجلد الإضافة الخاص بها فقط (rin_extensions/) وما تسمح به أذوناتها أعلاه."
        ExtensionType.LIBRARY -> "مجلد lib/ في المشروع الذي تُثبَّت فيه فقط."
        ExtensionType.THEME -> "إعدادات الألوان والتنسيق في المحرر، دون وصول لملفات المشروع."
        ExtensionType.UI_COMPONENT -> "عناصر واجهة المحرر (لوحات، أزرار، شرائط أدوات) فقط."
        ExtensionType.DEBUG_TOOL -> "ناتج التشغيل (Console) وسجلات التنفيذ فقط."
        ExtensionType.AI_TOOL -> "محتوى الكود الحالي، وقد تتصل بالإنترنت إن مُنحت إذن network.access."
        ExtensionType.TEMPLATE -> "تُستخدم فقط عند إنشاء مشروع جديد، ولا تصل لمشاريعك الحالية."
    }

    /**
     * توقيع رقمي مبسّط: بصمة SHA-256 لمحتوى الإضافة (base64Data) كاملاً. هذا تحقّق سلامة/تطابق
     * محتوى وليس توقيعاً رقمياً بشهادة معتمدة (لا توجد بنية مفاتيح عامة/خاصة هنا) — يُعرَض
     * للمستخدم بصراحة بهذا الوصف في شاشة الأمان حتى لا يُفهَم كضمان هوية مطوّر موثَّق.
     */
    fun computeSignature(base64Data: String): String {
        val digest = MessageDigest.getInstance("SHA-256").digest(base64Data.toByteArray(Charsets.UTF_8))
        return digest.joinToString("") { "%02x".format(it) }
    }

    /** يتحقق أن [expectedSignature] المعلَنة تطابق بصمة المحتوى الفعلي [base64Data]. */
    fun verifySignature(base64Data: String, expectedSignature: String): Boolean {
        if (expectedSignature.isBlank()) return false
        return computeSignature(base64Data).equals(expectedSignature, ignoreCase = true)
    }

    /** يقصّر التوقيع لعرضه في الواجهة، مثال: "3f9a2c81…e04b17". */
    fun shortSignature(signature: String): String {
        if (signature.length <= 20) return signature
        return "${signature.take(10)}…${signature.takeLast(6)}"
    }
}
