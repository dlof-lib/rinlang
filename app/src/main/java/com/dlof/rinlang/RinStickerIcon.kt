package com.dlof.rinlang

import java.io.File

/**
 * يجعل حاوية @sticker/@container.sticker مسؤولة تلقائياً عن "أيقونة التطبيق": يبحث في مصدر
 * مشروع Rin عن أول حاوية sticker تحمل حقل icon (النموذج الموثَّق في rin_ast.h):
 *
 *   @sticker=name              text icon = "assets/icon.png";  ...  .end/sticker
 *   @container.sticker=name    text icon = "assets/icon.png";  ...  .end/container.sticker
 *
 * ويحلّ قيمة هذا الحقل إلى ملف صورة حقيقي داخل مجلد المشروع. لا حاجة لتشغيل المفسِّر (Interpreter)
 * فعلياً: تصدير APK أصلاً لا يُنفِّذ المشروع وقت البناء (انظر توثيق [RinApkExporter] — التنفيذ
 * الحقيقي يحدث لاحقاً داخل الحزمة المُصدَّرة عبر ExportedRunActivity)، فمسح نصّي خفيف يطابق نفس
 * أسلوب الأدوات الأخرى في هذا الملف تماماً (project files as-is)، بلا فرق دلالي عن قراءة الحقل
 * عبر المفسِّر لأن 'icon' هنا نص حرفي بسيط دوماً في كل الأمثلة الموثَّقة.
 *
 * هذا المنطق نفسه (نفس الصياغة، نفس اسم الحقل) مُعاد تطبيقه بصيغة JavaScript في
 * web/rinhtml/rinhtml.js (findStickerIconPath) لضبط الأيقونة المفضّلة (favicon) لموقع RinHTML —
 * حتى يبقى "أيقونة التطبيق" مفهوماً واحداً من مصدر واحد (حقل sticker.icon) عبر الوجهتين: تصدير
 * APK (هذا الملف)، والموقع (RinHTML).
 */
object RinStickerIcon {

    // يطابق أول حاوية @sticker=name أو @container.sticker=name حتى غلقها .end/sticker المقابل
    // — بلا حساسية لحالة الأحرف في اسم الحاوية نفسها، وبلا اشتراط ترتيب معيّن لحقولها الداخلية.
    private val STICKER_BLOCK = Regex(
        "@(?:container\\.)?sticker\\s*=\\s*\"?[A-Za-z_][\\w.]*\"?[\\s\\S]*?\\.end/(?:container\\.)?sticker",
        RegexOption.IGNORE_CASE
    )
    // حقل 'icon' كنص عادي (text icon = "..."; أو let icon = "...";) بنفس دعم التهريب البسيط
    // المستخدم في rin_lexer.cpp::scanString (\" \\).
    private val ICON_FIELD = Regex("\\bicon\\s*=\\s*\"((?:\\\\.|[^\"\\\\])*)\"")

    /** مسار الأيقونة المُعلَن داخل أول حاوية sticker في [source]، أو null إن لم توجد حاوية sticker
     *  أو لم يكن لها حقل icon. */
    fun findIconPath(source: String): String? {
        val block = STICKER_BLOCK.find(source)?.value ?: return null
        val raw = ICON_FIELD.find(block)?.groupValues?.get(1) ?: return null
        return raw.replace("\\\"", "\"").replace("\\\\", "\\").trim().ifBlank { null }
    }

    /**
     * يبحث في كل ملفات .rin بمشروع [project] عن أول أيقونة sticker (يُفضَّل [entryFileName] إن
     * وُجد لأن هذا عادة الملف الذي يُعرِّف حاوية التطبيق الرئيسية)، ثم يحلّها إلى ملف حقيقي داخل
     * جذر المشروع؛ null إن لم يُعثر على أي حقل icon صالح أو كان الملف المُشار إليه غير موجود فعلياً.
     */
    fun findIconFile(project: Project, entryFileName: String? = null): File? {
        val files = ProjectManager.listFiles(project).filter { it.name.endsWith(".rin") }
        val ordered = if (entryFileName != null) files.sortedByDescending { it.name == entryFileName } else files
        for (rinFile in ordered) {
            val path = try { findIconPath(rinFile.file.readText()) } catch (e: Exception) { null } ?: continue
            val resolved = File(project.dir, path)
            if (resolved.exists() && resolved.isFile) return resolved
        }
        return null
    }
}
