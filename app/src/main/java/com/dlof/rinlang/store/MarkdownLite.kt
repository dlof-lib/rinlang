package com.dlof.rinlang.store

/**
 * محوّل Markdown → HTML خفيف الوزن، يكفي لعرض ملفات README.md المنشورة في متجر Rin
 * (عناوين، **تشديد**، `كود مضمَّن`، كتل كود ```، قوائم -، فواصل ---) داخل TextView واحد عبر
 * android.text.Html.fromHtml، دون إضافة مكتبة خارجية جديدة للمشروع.
 */
object MarkdownLite {

    fun toHtml(markdown: String): String {
        val escaped = markdown
            .replace("&", "&amp;")
            .replace("<", "&lt;")
            .replace(">", "&gt;")

        val html = StringBuilder()
        var inCodeBlock = false
        val codeBlockBuffer = StringBuilder()
        var inList = false

        fun closeListIfOpen() {
            if (inList) {
                html.append("</ul>")
                inList = false
            }
        }

        for (rawLine in escaped.lines()) {
            val line = rawLine

            if (line.trim().startsWith("```")) {
                if (inCodeBlock) {
                    html.append("<pre><code>").append(codeBlockBuffer).append("</code></pre>")
                    codeBlockBuffer.clear()
                    inCodeBlock = false
                } else {
                    closeListIfOpen()
                    inCodeBlock = true
                }
                continue
            }
            if (inCodeBlock) {
                codeBlockBuffer.append(line).append("\n")
                continue
            }

            val trimmed = line.trim()
            when {
                trimmed.isEmpty() -> {
                    closeListIfOpen()
                    html.append("<br>")
                }
                trimmed == "---" || trimmed == "***" -> {
                    closeListIfOpen()
                    html.append("<hr>")
                }
                trimmed.startsWith("#### ") -> { closeListIfOpen(); html.append("<h4>").append(inlineFormat(trimmed.removePrefix("#### "))).append("</h4>") }
                trimmed.startsWith("### ") -> { closeListIfOpen(); html.append("<h3>").append(inlineFormat(trimmed.removePrefix("### "))).append("</h3>") }
                trimmed.startsWith("## ") -> { closeListIfOpen(); html.append("<h2>").append(inlineFormat(trimmed.removePrefix("## "))).append("</h2>") }
                trimmed.startsWith("# ") -> { closeListIfOpen(); html.append("<h1>").append(inlineFormat(trimmed.removePrefix("# "))).append("</h1>") }
                trimmed.startsWith("- ") || trimmed.startsWith("* ") -> {
                    if (!inList) { html.append("<ul>"); inList = true }
                    html.append("<li>").append(inlineFormat(trimmed.drop(2))).append("</li>")
                }
                else -> {
                    closeListIfOpen()
                    html.append("<p>").append(inlineFormat(trimmed)).append("</p>")
                }
            }
        }
        closeListIfOpen()
        if (inCodeBlock) {
            html.append("<pre><code>").append(codeBlockBuffer).append("</code></pre>")
        }
        return html.toString()
    }

    /** يطبّق **تشديد** و`كود مضمَّن` داخل سطر واحد بعد أن أصبح خالياً من أحرف HTML الخاصة أصلاً. */
    private fun inlineFormat(text: String): String {
        var result = text
        result = Regex("\\*\\*(.+?)\\*\\*").replace(result) { "<b>${it.groupValues[1]}</b>" }
        result = Regex("`(.+?)`").replace(result) { "<code>${it.groupValues[1]}</code>" }
        return result
    }
}
