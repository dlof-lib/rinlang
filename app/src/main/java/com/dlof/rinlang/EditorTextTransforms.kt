package com.dlof.rinlang

/**
 * تحويلات نصّية خالصة تُطبَّق على محتوى المحرر عند الحفظ (حسب إعدادات "الحفظ"):
 * تنسيق الملف، حذف المسافات الزائدة في نهاية الأسطر، وضمان سطر جديد أخير.
 *
 * لا تعتمد على أي شيء من أندرويد، فهي قابلة للاختبار مباشرة، وكل دالة فيها idempotent
 * (تطبيقها مرتين يعطي نفس نتيجة تطبيقها مرة واحدة)، كي لا يتغيّر الملف في كل حفظ متتالٍ.
 */
object EditorTextTransforms {

    /** خيارات الحفظ المفعَّلة؛ [isNoop] = لا شيء يُغيَّر فيُتجاوز العمل كله. */
    data class SaveOptions(
        val format: Boolean,
        val trimTrailingWhitespace: Boolean,
        val ensureFinalNewline: Boolean,
        val tabSize: Int
    ) {
        val isNoop: Boolean get() = !format && !trimTrailingWhitespace && !ensureFinalNewline
    }

    fun applyOnSave(text: String, options: SaveOptions): String {
        if (options.isNoop) return text
        var out = text
        if (options.format) out = leadingTabsToSpaces(normalizeLineEndings(out), options.tabSize)
        if (options.trimTrailingWhitespace) out = trimTrailingWhitespace(out)
        if (options.ensureFinalNewline) out = ensureFinalNewline(out)
        return out
    }

    /** يوحّد نهايات الأسطر إلى `\n` (يحوّل `\r\n` و`\r` المنفردة). */
    fun normalizeLineEndings(text: String): String =
        if (text.indexOf('\r') < 0) text else text.replace("\r\n", "\n").replace('\r', '\n')

    /** يحوّل التبويبات في **بداية** كل سطر فقط إلى مسافات (حتى نقطة التوقف التالية)؛ ما بعد الإزاحة لا يُمَسّ. */
    fun leadingTabsToSpaces(text: String, tabSize: Int): String {
        if (text.indexOf('\t') < 0) return text
        val size = tabSize.coerceIn(1, 16)
        return mapLines(text) { line ->
            var end = 0
            while (end < line.length && (line[end] == ' ' || line[end] == '\t')) end++
            val indent = line.substring(0, end)
            if (indent.indexOf('\t') < 0) line else expandTabs(indent, size) + line.substring(end)
        }
    }

    /** يحذف المسافات والتبويبات في نهاية كل سطر (ويحافظ على `\r\n` إن كان الملف يستعملها). */
    fun trimTrailingWhitespace(text: String): String =
        mapLines(text) { line -> line.trimEnd(' ', '\t') }

    /** يجعل الملف ينتهي بسطر جديد واحد بالضبط. الملف الفارغ يبقى فارغًا. */
    fun ensureFinalNewline(text: String): String {
        if (text.isEmpty()) return text
        val newline = if (text.contains("\r\n")) "\r\n" else "\n"
        val body = text.trimEnd('\n', '\r')
        return if (body.isEmpty()) newline else body + newline
    }

    private fun expandTabs(indent: String, size: Int): String {
        val sb = StringBuilder(indent.length + size)
        for (c in indent) {
            if (c == '\t') repeat(size - (sb.length % size)) { sb.append(' ') } else sb.append(c)
        }
        return sb.toString()
    }

    /** يطبّق [transform] على كل سطر مع حفظ الفواصل الأصلية (`\n` أو `\r\n`) كما هي. */
    private fun mapLines(text: String, transform: (String) -> String): String {
        val parts = text.split('\n')
        val sb = StringBuilder(text.length)
        for ((index, raw) in parts.withIndex()) {
            val hasCr = raw.endsWith('\r')
            val line = if (hasCr) raw.dropLast(1) else raw
            sb.append(transform(line))
            if (hasCr) sb.append('\r')
            if (index < parts.lastIndex) sb.append('\n')
        }
        return sb.toString()
    }
}
