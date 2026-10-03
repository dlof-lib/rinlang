package com.dlof.rinlang

import android.content.Context
import org.json.JSONObject
import java.io.File

/**
 * منطق تشغيل مشروع ويب (index.html) مشترك بين [HtmlRunActivity] (WebView داخل التطبيق) و[RinLocalServer]
 * (المتصفح على http://localhost:7700). يقوم بثلاثة أشياء:
 *
 *  1) يختار ملف المنطق ويُنشئ جلسة Rin حيّة ([RinEngine.HtmlSession]) — أو لا يُنشئ شيئاً لمشروع «HTML + JS»
 *     الذي منطقه JavaScript في الصفحة نفسها (صفحة عادية بلا جسر).
 *  2) يبني الصفحة: يدمج style.css وأي <script src="..."> محلي داخلها، ثم يحقن rin_html_runtime.js
 *     فقط إن وُجدت جلسة Rin.
 *  3) يحفظ/يستعيد الحالة (persist=) في <المشروع>/.rin_state/.
 *
 * ترتيب اختيار ملف المنطق: الملف المُمرَّر صراحةً ← <link rel="rin"> / <meta name="rin"> ← ملف حاوية موقَّع
 * ← container.rin ← main.rin. كل المسارات محصورة داخل مجلد المشروع.
 */
class HtmlProjectRuntime(
    private val context: Context,
    val project: Project,
    private val explicitLogic: String? = null,
    private val log: (String) -> Unit = {}
) {
    var session: RinEngine.HtmlSession? = null
        private set
    var logicFile: File? = null
        private set
    private var header: RinContainerFile.Header? = null
    private var lastSavedState: String? = null
    private var html: String = ""

    /** true عندما لا توجد جلسة Rin (مشروع HTML + JS): الصفحة تعمل وحدها. */
    val isPlainPage: Boolean get() = session == null

    /** يفتح المشروع. يعيد نص الخطأ إن فشل، أو null عند النجاح. */
    fun open(): String? {
        close()
        val htmlFile = File(project.dir, HtmlRunActivity.ENTRY_HTML)
        if (!htmlFile.isFile) return context.getString(R.string.html_run_no_index)
        html = htmlFile.readText()

        val logic = resolveLogicFile(html)
        if (logic == null) {
            // مشروع HTML + JS (أو أي مشروع بلا ملف Rin): صفحة عادية. مشروع HTML + Rin بلا منطق = خطأ.
            return if (project.type == ProjectType.HTML) context.getString(R.string.html_run_no_logic) else null
        }
        logicFile = logic
        return try {
            val source = logic.readText()
            header = RinContainerFile.parseHeader(source)
            val s = RinEngine.HtmlSession.create(source, project.dir.absolutePath)
            session = s
            if (s.bootOutput.isNotBlank()) log(s.bootOutput.trimEnd())
            restoreState(s)
            null
        } catch (t: Throwable) {
            context.getString(R.string.html_run_rin_error, t.message ?: "")
        }
    }

    fun close() {
        session?.close()
        session = null
        logicFile = null
        header = null
    }

    // ---- ملف المنطق ----

    private fun inside(rel: String?): File? {
        if (rel.isNullOrBlank() || rel.contains("://") || rel.startsWith("/")) return null
        val root = project.dir.canonicalFile
        val f = File(project.dir, rel.trim()).canonicalFile
        return f.takeIf { it.path.startsWith(root.path + File.separator) && it.isFile }
    }

    private fun resolveLogicFile(html: String): File? {
        inside(explicitLogic)?.let { return it }
        val linkRe = Regex("""<link\b[^>]*rel\s*=\s*["']rin["'][^>]*>""", RegexOption.IGNORE_CASE)
        val hrefRe = Regex("""href\s*=\s*["']([^"']+)["']""", RegexOption.IGNORE_CASE)
        val metaRe = Regex("""<meta\b[^>]*name\s*=\s*["']rin["'][^>]*>""", RegexOption.IGNORE_CASE)
        val contentRe = Regex("""content\s*=\s*["']([^"']+)["']""", RegexOption.IGNORE_CASE)
        val declared = linkRe.find(html)?.value?.let { hrefRe.find(it)?.groupValues?.get(1) }
            ?: metaRe.find(html)?.value?.let { contentRe.find(it)?.groupValues?.get(1) }
        inside(declared)?.let { return it }
        RinContainerFile.findIn(project.dir)?.let { return it }
        return inside(HtmlRunActivity.ENTRY_LOGIC) ?: inside("main.rin")
    }

    // ---- بناء الصفحة ----

    /** نص rin_html_runtime.js من assets، أو null (مع تسجيل الخطأ) إن تعذّرت قراءته. */
    fun readRuntimeAsset(): String? = try {
        context.assets.open("rin_html_runtime.js").bufferedReader().use { it.readText() }
    } catch (t: Throwable) {
        log("تعذّر تحميل rin_html_runtime.js")
        null
    }

    /**
     * يبني الصفحة النهائية. [prelude] سكربت يُحقن قبل السكربت الأساسي (مثل RIN_HTTP_BASE للخادم المحلي).
     * يُحقن rin_html_runtime.js فقط عند وجود جلسة Rin.
     */
    fun buildPage(prelude: String = ""): String {
        val linkRe = Regex("""<link\b[^>]*rel\s*=\s*["']stylesheet["'][^>]*>""", RegexOption.IGNORE_CASE)
        val hrefRe = Regex("""href\s*=\s*["']([^"']+)["']""", RegexOption.IGNORE_CASE)
        var out = linkRe.replace(html) { m ->
            val href = hrefRe.find(m.value)?.groupValues?.get(1)
            val css = href?.let { readProjectText(it) }
            if (css != null) "<style>\n$css\n</style>" else m.value
        }
        // لا رابط style.css في الصفحة؟ طبّق style.css تلقائياً إن وُجد.
        if (!linkRe.containsMatchIn(html)) {
            readProjectText("style.css")?.let { css ->
                val tag = "<style>\n$css\n</style>"
                out = if (out.contains("</head>", true)) insertBeforeFirst(out, "</head>", tag) else tag + out
            }
        }

        // <script src="script.js"> محلي → يُدمج داخل الصفحة (كما CSS) ليعمل بلا وصول ملفات. السكربتات
        // ذات defer تُنقل إلى آخر <body> لتحافظ على سلوكها (تنفَّذ بعد بناء الصفحة).
        val deferred = StringBuilder()
        val scriptRe = Regex("""<script\b([^>]*)>\s*</script>""", RegexOption.IGNORE_CASE)
        val srcRe = Regex("""\s*\bsrc\s*=\s*["']([^"']+)["']""", RegexOption.IGNORE_CASE)
        out = scriptRe.replace(out) { m ->
            val attrs = m.groupValues[1]
            val src = srcRe.find(attrs)?.groupValues?.get(1)
            val js = src?.let { readProjectText(it) }
            if (js == null) {
                m.value
            } else {
                val safe = js.replace("</script", "<\\/script", ignoreCase = true)
                val rest = srcRe.replace(attrs, "")
                val isDefer = Regex("""\bdefer\b""", RegexOption.IGNORE_CASE).containsMatchIn(rest)
                val cleaned = if (isDefer) Regex("""\s*\bdefer\b""", RegexOption.IGNORE_CASE).replace(rest, "") else rest
                val tag = "<script$cleaned>\n$safe\n</script>"
                if (isDefer) { deferred.append(tag).append('\n'); "" } else tag
            }
        }

        val tail = StringBuilder(deferred)
        if (session != null) {
            val runtime = readRuntimeAsset()
            if (runtime != null) {
                if (prelude.isNotEmpty()) tail.insert(0, prelude + "\n")
                tail.append("<script>").append(runtime.replace("</script", "<\\/script", ignoreCase = true)).append("</script>")
            }
        }
        val injected = tail.toString()
        if (injected.isEmpty()) return out
        return if (out.contains("</body>", true)) insertBeforeLast(out, "</body>", injected) else out + injected
    }

    /** يُدرج [insert] قبل أول ظهور لـ [marker] (دون حساسية لحالة الأحرف) كنص حرفي. */
    private fun insertBeforeFirst(src: String, marker: String, insert: String): String {
        val i = src.indexOf(marker, ignoreCase = true)
        return if (i < 0) src + insert else src.substring(0, i) + insert + src.substring(i)
    }

    private fun insertBeforeLast(src: String, marker: String, insert: String): String {
        val i = src.lastIndexOf(marker, ignoreCase = true)
        return if (i < 0) src + insert else src.substring(0, i) + insert + src.substring(i)
    }

    /** يقرأ ملفاً نصياً من داخل المشروع فقط (يمنع الخروج منه عبر ../ أو مسار مطلق/URL). */
    fun readProjectText(rel: String): String? {
        if (rel.contains("://") || rel.startsWith("/") || rel.startsWith("data:") || rel.startsWith("//")) return null
        val f = File(project.dir, rel.substringBefore('?').substringBefore('#')).canonicalFile
        val root = project.dir.canonicalFile
        if (!f.path.startsWith(root.path + File.separator) || !f.isFile) return null
        return f.readText()
    }

    // ---- الجسر ----

    fun globalsJson(): String = session?.globalsJson() ?: "{}"

    fun call(fn: String, argsJson: String): String {
        val s = session ?: return "{\"ok\":false,\"error\":\"no session\",\"globals\":{}}"
        val result = s.call(fn, argsJson)
        if (header?.persists == true) saveState(s.globalsJson())
        return result
    }

    fun setGlobal(name: String, valueJson: String) {
        val s = session ?: return
        s.setGlobal(name, valueJson)
        if (header?.persists == true && shouldPersist(name)) saveState(s.globalsJson())
    }

    fun clearSavedState() {
        stateFile()?.delete()
        lastSavedState = null
    }

    // ---- حفظ الحالة (persist=) ----

    private fun stateFile(): File? = logicFile?.let { File(File(project.dir, ".rin_state"), it.name + ".json") }

    private fun shouldPersist(name: String): Boolean {
        val h = header ?: return false
        return if (h.persistAll) name !in RinContainerFile.BUILTIN_GLOBALS else name in h.persist
    }

    private fun restoreState(s: RinEngine.HtmlSession) {
        val file = stateFile()?.takeIf { header?.persists == true && it.isFile } ?: return
        try {
            val saved = JSONObject(file.readText())
            val current = JSONObject(s.globalsJson())
            val keys = saved.keys()
            while (keys.hasNext()) {
                val k = keys.next()
                if (!current.has(k) || !shouldPersist(k)) continue
                s.setGlobal(k, toJsonLiteral(saved.get(k)))
            }
            lastSavedState = file.readText()
        } catch (t: Throwable) {
            log("تعذّرت استعادة الحالة المحفوظة: ${t.message}")
        }
    }

    private fun saveState(globalsJson: String) {
        if (header?.persists != true) return
        val file = stateFile() ?: return
        try {
            val all = JSONObject(globalsJson)
            val out = JSONObject()
            val keys = all.keys()
            while (keys.hasNext()) { val k = keys.next(); if (shouldPersist(k)) out.put(k, all.get(k)) }
            val text = out.toString()
            if (text == lastSavedState) return
            file.parentFile?.mkdirs()
            file.writeText(text)
            lastSavedState = text
        } catch (t: Throwable) {
            log("تعذّر حفظ الحالة: ${t.message}")
        }
    }

    companion object {
        /** قيمة JSON (كما يقرؤها org.json) → نص حرفي JSON صالح لـ [RinEngine.HtmlSession.setGlobal]. */
        fun toJsonLiteral(v: Any?): String = when (v) {
            null -> "null"
            is String -> JSONObject.quote(v)
            else -> v.toString() // JSONObject / JSONArray / Number / Boolean / JSONObject.NULL
        }
    }
}
