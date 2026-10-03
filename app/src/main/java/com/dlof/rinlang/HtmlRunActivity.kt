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
    private lateinit var runtime: HtmlProjectRuntime
    private lateinit var web: WebView
    private lateinit var consoleScroll: ScrollView
    private lateinit var consoleText: TextView
    private val consoleLines = ArrayList<String>()

    @SuppressLint("SetJavaScriptEnabled")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val name = intent.getStringExtra(EXTRA_PROJECT_NAME)
        val found = name?.let { n -> ProjectManager.listProjects(this).find { it.name == n } }
        if (found == null) { finish(); return }
        project = found
        runtime = HtmlProjectRuntime(this, project, intent.getStringExtra(EXTRA_LOGIC_FILE)) { log(it) }
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
        // فتح المشروع في متصفح الجهاز على http://localhost:7700 (خادم محلي، انظر RinLocalServer).
        val browser = ImageButton(this).apply {
            setImageResource(R.drawable.ic_globe_link)
            setBackgroundColor(Color.TRANSPARENT)
            contentDescription = getString(R.string.html_run_open_browser)
            setOnClickListener { RinLocalServer.openInBrowser(this@HtmlRunActivity, project.name) }
        }
        val reload = ImageButton(this).apply {
            setImageResource(android.R.drawable.ic_popup_sync)
            setBackgroundColor(Color.TRANSPARENT)
            contentDescription = getString(R.string.html_run_reload)
            setOnClickListener { loadProject() }
            // ضغطة مطوّلة: مسح الحالة المحفوظة (persist=) ثم إعادة التحميل من جديد.
            setOnLongClickListener {
                runtime.clearSavedState()
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
        bar.addView(browser, LinearLayout.LayoutParams(dp(44), dp(44)))
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

    /** يعيد فتح المشروع (جلسة Rin جديدة إن وُجد منطق Rin) ويعيد تحميل الصفحة بحالة نظيفة. */
    private fun loadProject() {
        consoleLines.clear()
        consoleScroll.visibility = View.GONE

        val error = runtime.open()
        if (error != null) { showFatal(error); return }

        web.removeJavascriptInterface("RinBridge")
        // مشروع HTML + JS: صفحة عادية، لا جسر Rin ولا runtime.
        if (!runtime.isPlainPage) web.addJavascriptInterface(Bridge(), "RinBridge")
        val page = runtime.buildPage()
        web.loadDataWithBaseURL(Uri.fromFile(project.dir).toString() + "/", page, "text/html", "UTF-8", null)
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
        @JavascriptInterface fun globals(): String = runtime.globalsJson()
        @JavascriptInterface fun call(fn: String, argsJson: String): String = runtime.call(fn, argsJson)
        @JavascriptInterface fun set(name: String, valueJson: String) { runtime.setGlobal(name, valueJson) }
        @JavascriptInterface fun clearState() { runtime.clearSavedState() }
        @JavascriptInterface fun error(msg: String) { log(msg) }
    }

    override fun onDestroy() {
        runtime.close()
        web.removeJavascriptInterface("RinBridge")
        web.destroy()
        super.onDestroy()
    }
}
