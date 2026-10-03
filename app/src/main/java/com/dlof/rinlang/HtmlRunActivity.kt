package com.dlof.rinlang

import android.annotation.SuppressLint
import android.content.Intent
import android.graphics.Color
import android.net.Uri
import android.os.Bundle
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.widget.Toast
import org.json.JSONObject
import android.view.ViewGroup
import android.webkit.ConsoleMessage
import android.webkit.JavascriptInterface
import android.webkit.WebChromeClient
import android.webkit.WebResourceRequest
import android.webkit.WebSettings
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.ImageButton
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import java.io.File

/**
 * يشغّل مشروع HTML: يعرض index.html داخل WebView ويربطه بملف منطق Rin عبر جلسة حيّة
 * ([RinEngine.HtmlSession]).
 *
 *   index.html     الواجهة (HTML)
 *   style.css      التنسيق — يُدمَج تلقائياً عند وجود <link rel="stylesheet" href="style.css">
 *   ملف الحاوية    ملف .rin يبدأ بالتوقيع `//! rin:container web` ([RinContainerFile]): متغيّرات عامة
 *                  (let/warp) ودوال علوية تُستدعى من الصفحة. يُعرَّف بتوقيعه لا باسمه (يستطيع أي مستخدم
 *                  تسمية ملف container.rin أو أي حاوية container)، والاسم container.rin مجرد عرف افتراضي.
 *
 * اختيار ملف المنطق بالترتيب: ملف مُمرَّر صراحةً (قائمة Run) ← <link rel="rin" href=".."> أو
 * <meta name="rin" content=".."> في index.html ← ملف حاوية موقَّع في جذر المشروع (container.rin أولاً)
 * ← container.rin ← main.rin.
 *
 * الربط من داخل index.html بلا JavaScript (تفاصيلها في assets/rin_html_runtime.js وdocs/html-projects.md):
 *   rin-text="expr"          نص من متغيّر أو مسار (user.name، todos.length)
 *   rin-click="f(a, b)"      استدعاء دالة Rin (الوسائط: حرفيات أو مسارات؛ داخل rin-for ترى عنصرها)
 *   rin-model="x"            ربط ثنائي الاتجاه لحقل إدخال
 *   rin-show / rin-if="e"    إظهار/إخفاء بتعبير (== != < > >= <= ! && ||)، وrin-else بعد rin-if
 *   rin-for="t, i in list"   تكرار عنصر لكل عنصر في مصفوفة (أو مفاتيح قاموس)
 *   rin-attr="href:u; title:t"  وrin-class="done:t.done"   خصائص وأصناف من الحالة
 * ومن JavaScript: window.rin.call/get/set/globals/clearState.
 * حفظ الحالة: `//! rin:container web persist=a,b` (أو persist=*) يحفظ تلك المتغيرات بين التشغيلات في
 * <المشروع>/.rin_state/<ملف>.json؛ ضغطة مطوّلة على زر إعادة التحميل تمسحها وتبدأ من جديد.
 * الملفات والشبكة: دوال Rin (readFile/writeFile/httpGet...) تعمل داخل ملف الحاوية كالمعتاد على مجلد المشروع.
 */
class HtmlRunActivity : AppCompatActivity() {

    companion object {
        const val EXTRA_PROJECT_NAME = "extra_project_name"
        /** مسار نسبي (اختياري) لملف Rin الذي يُربط بالصفحة؛ يتقدّم على ما يحدده index.html. */
        const val EXTRA_LOGIC_FILE = "extra_logic_file"
        const val ENTRY_HTML = "index.html"
        const val ENTRY_LOGIC = "container.rin"

        /** يفتح المشروع [projectName] في شاشة التشغيل. */
        fun start(context: android.content.Context, projectName: String, logicFile: String? = null) {
            context.startActivity(Intent(context, HtmlRunActivity::class.java)
                .putExtra(EXTRA_PROJECT_NAME, projectName)
                .putExtra(EXTRA_LOGIC_FILE, logicFile))
        }

        /** هل هذا المشروع قابل للتشغيل كصفحة HTML (فيه index.html)؟ */
        fun isHtmlProject(project: Project): Boolean = File(project.dir, ENTRY_HTML).isFile
    }

    private lateinit var project: Project
    private lateinit var web: WebView
    private lateinit var consoleScroll: ScrollView
    private lateinit var consoleText: TextView
    private var session: RinEngine.HtmlSession? = null
    private var logicFile: File? = null
    private var header: RinContainerFile.Header? = null
    private var lastSavedState: String? = null
    private val consoleLines = ArrayList<String>()

    @SuppressLint("SetJavaScriptEnabled")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val name = intent.getStringExtra(EXTRA_PROJECT_NAME)
        val found = name?.let { n -> ProjectManager.listProjects(this).find { it.name == n } }
        if (found == null) { finish(); return }
        project = found
        title = getString(R.string.html_run_title, project.name)

        val root = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }

        // شريط علوي بسيط: العنوان + إعادة تحميل + إغلاق.
        val bar = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setBackgroundColor(Color.parseColor("#14141C"))
            setPadding(dp(12), dp(4), dp(4), dp(4))
        }
        val titleView = TextView(this).apply {
            text = title
            setTextColor(Color.parseColor("#ECEEF7"))
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 15f)
            maxLines = 1
            layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
        }
        val reload = ImageButton(this).apply {
            setImageResource(android.R.drawable.ic_popup_sync)
            setBackgroundColor(Color.TRANSPARENT)
            contentDescription = getString(R.string.html_run_reload)
            setOnClickListener { loadProject() }
            // ضغطة مطوّلة: مسح الحالة المحفوظة (persist=) ثم إعادة التحميل من جديد.
            setOnLongClickListener {
                clearSavedState()
                Toast.makeText(this@HtmlRunActivity, getString(R.string.html_run_state_cleared), Toast.LENGTH_SHORT).show()
                loadProject()
                true
            }
        }
        val close = ImageButton(this).apply {
            setImageResource(android.R.drawable.ic_menu_close_clear_cancel)
            setBackgroundColor(Color.TRANSPARENT)
            contentDescription = getString(R.string.html_run_close)
            setOnClickListener { finish() }
        }
        bar.addView(titleView)
        bar.addView(reload, LinearLayout.LayoutParams(dp(44), dp(44)))
        bar.addView(close, LinearLayout.LayoutParams(dp(44), dp(44)))
        root.addView(bar, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))

        web = WebView(this).apply {
            settings.javaScriptEnabled = true
            settings.domStorageEnabled = true
            settings.allowFileAccess = true
            settings.allowFileAccessFromFileURLs = false
            settings.allowUniversalAccessFromFileURLs = false
            settings.cacheMode = WebSettings.LOAD_NO_CACHE
            webViewClient = object : WebViewClient() {
                override fun shouldOverrideUrlLoading(view: WebView, request: WebResourceRequest): Boolean {
                    val scheme = request.url.scheme ?: return false
                    if (scheme == "http" || scheme == "https") {
                        runCatching { startActivity(Intent(Intent.ACTION_VIEW, request.url)) }
                        return true
                    }
                    return false
                }
            }
            webChromeClient = object : WebChromeClient() {
                override fun onConsoleMessage(m: ConsoleMessage): Boolean {
                    if (m.messageLevel() == ConsoleMessage.MessageLevel.ERROR) log("JS: ${m.message()}")
                    return true
                }
            }
        }
        root.addView(web, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f))

        // لوحة صغيرة لمخرجات print() الأولية وأخطاء Rin؛ مخفية ما لم يوجد ما يُعرض.
        consoleText = TextView(this).apply {
            setTextColor(Color.parseColor("#C8C8D8"))
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 12f)
            typeface = android.graphics.Typeface.MONOSPACE
            setPadding(dp(12), dp(6), dp(12), dp(6))
        }
        consoleScroll = ScrollView(this).apply {
            setBackgroundColor(Color.parseColor("#0A0A10"))
            visibility = View.GONE
            addView(consoleText)
        }
        root.addView(consoleScroll, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(110)))

        setContentView(root)
        loadProject()
    }

    /** يعيد إنشاء جلسة Rin من container.rin ويعيد تحميل الصفحة (حالة نظيفة). */
    private fun loadProject() {
        consoleLines.clear()
        consoleScroll.visibility = View.GONE
        session?.close()
        session = null

        val htmlFile = File(project.dir, ENTRY_HTML)
        if (!htmlFile.isFile) { showFatal(getString(R.string.html_run_no_index)); return }

        val logic = resolveLogicFile(htmlFile.readText())
        if (logic == null) { showFatal(getString(R.string.html_run_no_logic)); return }
        logicFile = logic
        try {
            val source = logic.readText()
            header = RinContainerFile.parseHeader(source)
            val s = RinEngine.HtmlSession.create(source, project.dir.absolutePath)
            session = s
            if (s.bootOutput.isNotBlank()) log(s.bootOutput.trimEnd())
            restoreState(s)
        } catch (t: Throwable) {
            showFatal(getString(R.string.html_run_rin_error, t.message ?: ""))
            return
        }

        web.removeJavascriptInterface("RinBridge")
        web.addJavascriptInterface(Bridge(), "RinBridge")
        val page = buildPage(htmlFile.readText())
        web.loadDataWithBaseURL(Uri.fromFile(project.dir).toString() + "/", page, "text/html", "UTF-8", null)
    }

    /**
     * أي ملف .rin يصلح كمنطق للصفحة، لا container.rin فقط. الأولوية:
     *  1) EXTRA_LOGIC_FILE (من قائمة Run)،
     *  2) <link rel="rin" href="app.rin"> أو <meta name="rin" content="app.rin"> في index.html،
     *  3) ملف حاوية موقَّع (//! rin:container web) في جذر المشروع، ثم container.rin ثم main.rin.
     * كل المسارات محصورة داخل مجلد المشروع.
     */
    private fun resolveLogicFile(html: String): File? {
        val root = project.dir.canonicalFile
        fun inside(rel: String?): File? {
            if (rel.isNullOrBlank() || rel.contains("://") || rel.startsWith("/")) return null
            val f = File(project.dir, rel.trim()).canonicalFile
            return f.takeIf { it.path.startsWith(root.path + File.separator) && it.isFile }
        }
        inside(intent.getStringExtra(EXTRA_LOGIC_FILE))?.let { return it }
        val linkRe = Regex("""<link\b[^>]*rel\s*=\s*["']rin["'][^>]*>""", RegexOption.IGNORE_CASE)
        val hrefRe = Regex("""href\s*=\s*["']([^"']+)["']""", RegexOption.IGNORE_CASE)
        val metaRe = Regex("""<meta\b[^>]*name\s*=\s*["']rin["'][^>]*>""", RegexOption.IGNORE_CASE)
        val contentRe = Regex("""content\s*=\s*["']([^"']+)["']""", RegexOption.IGNORE_CASE)
        val declared = linkRe.find(html)?.value?.let { hrefRe.find(it)?.groupValues?.get(1) }
            ?: metaRe.find(html)?.value?.let { contentRe.find(it)?.groupValues?.get(1) }
        inside(declared)?.let { return it }
        RinContainerFile.findIn(project.dir)?.let { return it }
        return inside(ENTRY_LOGIC) ?: inside("main.rin")
    }

    /** يدمج style.css (أو أي <link rel=stylesheet> محلي) داخل الصفحة ويحقن سكربت الربط. */
    private fun buildPage(html: String): String {
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
                out = if (out.contains("</head>", true)) out.replaceFirst(Regex("</head>", RegexOption.IGNORE_CASE)) { tag + "</head>" } // lambda: النص حرفي (لا تفسير لـ $ أو \)
                else tag + out
            }
        }
        val runtime = try { assets.open("rin_html_runtime.js").bufferedReader().use { it.readText() } }
            catch (t: Throwable) { log("تعذّر تحميل rin_html_runtime.js"); "" }
        val script = "<script>$runtime</script>"
        return if (out.contains("</body>", true))
            out.replaceFirst(Regex("</body>", RegexOption.IGNORE_CASE)) { script + "</body>" }
        else out + script
    }

    /** يقرأ ملفاً نصياً من داخل المشروع فقط (يمنع الخروج منه عبر ../ أو مسار مطلق/URL). */
    private fun readProjectText(rel: String): String? {
        if (rel.contains("://") || rel.startsWith("/") || rel.startsWith("data:")) return null
        val f = File(project.dir, rel.substringBefore('?').substringBefore('#')).canonicalFile
        val root = project.dir.canonicalFile
        if (!f.path.startsWith(root.path + File.separator) || !f.isFile) return null
        return f.readText()
    }

    // ---- حفظ الحالة (persist=) ----

    private fun stateFile(): File? = logicFile?.let { File(File(project.dir, ".rin_state"), it.name + ".json") }

    /** هل يُحفظ المتغيّر [name] وفق ترويسة ملف الحاوية؟ */
    private fun shouldPersist(name: String): Boolean {
        val h = header ?: return false
        return if (h.persistAll) name !in RinContainerFile.BUILTIN_GLOBALS else name in h.persist
    }

    /** يعيد القيم المحفوظة إلى الجلسة الجديدة (لمتغيّرات موجودة أصلاً فقط). */
    private fun restoreState(s: RinEngine.HtmlSession) {
        val file = stateFile()?.takeIf { header?.persists == true && it.isFile } ?: return
        try {
            val saved = JSONObject(file.readText())
            val current = JSONObject(s.globalsJson())
            val keys = saved.keys()
            while (keys.hasNext()) {
                val k = keys.next()
                if (!current.has(k) || !shouldPersist(k)) continue
                val v = saved.get(k)
                s.setGlobal(k, if (v is String) JSONObject.quote(v) else v.toString())
            }
            lastSavedState = file.readText()
        } catch (t: Throwable) {
            log("تعذّرت استعادة الحالة المحفوظة: ${t.message}")
        }
    }

    /** يحفظ المتغيّرات المطلوبة من [globalsJson] إن تغيّرت منذ آخر حفظ. */
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

    private fun clearSavedState() {
        stateFile()?.delete()
        lastSavedState = null
    }

    private fun showFatal(msg: String) {
        log(msg)
        web.loadDataWithBaseURL(null,
            "<html dir='rtl'><body style='background:#0a0a10;color:#ff8a8a;font-family:sans-serif;padding:24px'>" +
                android.text.TextUtils.htmlEncode(msg) + "</body></html>", "text/html", "UTF-8", null)
    }

    private fun log(line: String) {
        runOnUiThread {
            consoleLines.add(line)
            if (consoleLines.size > 200) consoleLines.removeAt(0)
            consoleText.text = consoleLines.joinToString("\n")
            consoleScroll.visibility = View.VISIBLE
            consoleScroll.post { consoleScroll.fullScroll(View.FOCUS_DOWN) }
        }
    }

    private fun dp(v: Int) = (v * resources.displayMetrics.density).toInt()

    /** الواجهة الوحيدة المكشوفة للصفحة (window.RinBridge). تعمل على خيط WebView الخلفي. */
    private inner class Bridge {
        @JavascriptInterface fun globals(): String = session?.globalsJson() ?: "{}"
        @JavascriptInterface fun call(fn: String, argsJson: String): String {
            val s = session ?: return "{\"ok\":false,\"error\":\"no session\",\"globals\":{}}"
            val result = s.call(fn, argsJson)
            if (header?.persists == true) saveState(s.globalsJson())
            return result
        }
        @JavascriptInterface fun set(name: String, valueJson: String) {
            val s = session ?: return
            s.setGlobal(name, valueJson)
            if (header?.persists == true && shouldPersist(name)) saveState(s.globalsJson())
        }
        @JavascriptInterface fun clearState() { clearSavedState() }
        @JavascriptInterface fun error(msg: String) { log(msg) }
    }

    override fun onDestroy() {
        session?.close()
        session = null
        web.removeJavascriptInterface("RinBridge")
        web.destroy()
        super.onDestroy()
    }
}
