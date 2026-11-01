package com.dlof.rinlang

import android.annotation.SuppressLint
import android.content.Context
import android.content.Intent
import android.graphics.Color
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.TypedValue
import android.view.Gravity
import android.view.ViewGroup
import android.webkit.WebSettings
import android.webkit.WebView
import android.widget.ImageButton
import android.widget.LinearLayout
import android.widget.TextView
import java.io.File
import java.util.concurrent.Executors

/**
 * معاينة حيّة لملفات PDF / Markdown / TypeScript (و JavaScript / JSON) داخل التطبيق.
 *
 * تُولَّد الصفحة بمكتبة `lib/previewkit.og.rin` المدمجة في المحرّك (pvPreview)، وتُعرَض في WebView.
 * المعاينة حيّة: كل ثانية تُقارَن بصمة الملف (الحجم + وقت التعديل)، وعند أي تغيير (حفظ من المحرّر مثلاً)
 * يُعاد التوليد في خيط خلفي مع الإبقاء على موضع التمرير.
 *
 * التشغيل: [start] (يُستدعى من زر Run في [MainActivity] عند فتح ملف .md/.pdf/.ts…).
 */
class FilePreviewActivity : RinBaseActivity() {

    companion object {
        const val EXTRA_PROJECT = "extra_preview_project"
        /** مسار الملف نسبةً لجذر المشروع (المحرّك يعزل كل المسارات داخل مجلد المشروع). */
        const val EXTRA_REL = "extra_preview_rel"
        private const val POLL_MS = 1000L
        /** الامتدادات التي يفتح لها Run المعاينة بدل تشغيل برنامج Rin. */
        val EXTENSIONS = setOf("md", "markdown", "pdf", "ts", "tsx", "mts", "cts")

        fun isPreviewable(name: String?): Boolean {
            val n = name?.lowercase() ?: return false
            if (n.endsWith(".d.ts")) return true
            return EXTENSIONS.contains(n.substringAfterLast('.', ""))
        }

        fun start(context: Context, projectName: String, relPath: String) {
            context.startActivity(Intent(context, FilePreviewActivity::class.java)
                .putExtra(EXTRA_PROJECT, projectName).putExtra(EXTRA_REL, relPath))
        }

        /** يحوّل نصاً إلى حرفية نصية Rin آمنة (تهريب \ و " وسطر جديد). */
        internal fun rinString(s: String): String {
            val sb = StringBuilder("\"")
            for (ch in s) when (ch) {
                '\\' -> sb.append("\\\\")
                '"' -> sb.append("\\\"")
                '\n' -> sb.append("\\n")
                '\r' -> sb.append("\\r")
                '\t' -> sb.append("\\t")
                else -> sb.append(ch)
            }
            return sb.append('"').toString()
        }
    }

    private lateinit var src: File
    private lateinit var out: File
    private lateinit var relSrc: String
    private lateinit var relOut: String
    private lateinit var web: WebView
    private lateinit var statusView: TextView
    private val handler = Handler(Looper.getMainLooper())
    private val worker = Executors.newSingleThreadExecutor()
    private var lastStamp = ""
    private var rendering = false
    private var live = true
    @Volatile private var closed = false

    private val poller = object : Runnable {
        override fun run() {
            if (closed) return
            if (live && !rendering && stampOf() != lastStamp) render()
            handler.postDelayed(this, POLL_MS)
        }
    }

    private fun stampOf(): String = if (src.isFile) "${src.length()}:${src.lastModified()}" else "missing"

    @SuppressLint("SetJavaScriptEnabled")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val pname = intent.getStringExtra(EXTRA_PROJECT)
        val rel = intent.getStringExtra(EXTRA_REL)
        val project = pname?.let { n -> ProjectManager.listProjects(this).find { it.name == n } }
        if (project == null || rel == null) { finish(); return }
        RinEngine.init(applicationContext, project.dir.absolutePath)
        relSrc = rel
        relOut = ".rin_preview/" + rel.replace('/', '_') + ".html"
        src = File(project.dir, rel)
        out = File(project.dir, relOut)
        title = src.name

        val root = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        val bar = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setBackgroundColor(Color.parseColor("#14141C"))
            setPadding(dp(12), dp(4), dp(4), dp(4))
        }
        val titleView = TextView(this).apply {
            text = src.name
            setTextColor(Color.parseColor("#ECEEF7"))
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 15f)
            maxLines = 1
            layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
        }
        statusView = TextView(this).apply {
            setTextColor(Color.parseColor("#8E93A8"))
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
            setPadding(dp(6), 0, dp(6), 0)
            text = getString(R.string.preview_live)
        }
        val reload = ImageButton(this).apply {
            setImageResource(android.R.drawable.ic_popup_sync)
            setBackgroundColor(Color.TRANSPARENT)
            contentDescription = getString(R.string.preview_reload)
            setOnClickListener { render() }
        }
        val close = ImageButton(this).apply {
            setImageResource(android.R.drawable.ic_menu_close_clear_cancel)
            setBackgroundColor(Color.TRANSPARENT)
            contentDescription = getString(R.string.preview_close)
            setOnClickListener { finish() }
        }
        bar.addView(titleView)
        bar.addView(statusView)
        bar.addView(reload, LinearLayout.LayoutParams(dp(44), dp(44)))
        bar.addView(close, LinearLayout.LayoutParams(dp(44), dp(44)))
        root.addView(bar, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))

        web = WebView(this).apply {
            settings.javaScriptEnabled = true
            settings.cacheMode = WebSettings.LOAD_NO_CACHE
            settings.allowFileAccess = false
            setBackgroundColor(Color.TRANSPARENT)
        }
        root.addView(web, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f))
        setContentView(root)
        render()
    }

    /** يولّد الصفحة في خيط خلفي ثم يعرضها مع الحفاظ على موضع التمرير. */
    private fun render() {
        if (rendering) return
        rendering = true
        val stamp = stampOf()
        val lang = if (java.util.Locale.getDefault().language == "ar") "ar" else "en"
        // المحرّك يعزل المسارات داخل مجلد المشروع، فنمرّر مساراً نسبياً للمصدر وللمخرَج (.rin_preview/).
        val script = "@import \"lib/previewkit.og.rin\";\n" +
            "let r = pvPreview(${rinString(relSrc)}, ${rinString(relOut)}, {\"lang\": ${rinString(lang)}});\n" +
            "print(r[\"ok\"]);\n"
        worker.execute {
            val res = RinEngine.runSourceStructured(script)
            val html = if (res.success && out.isFile) out.readText() else null
            val err = if (res.success) null else (res.errorMessage ?: res.output)
            runOnUiThread {
                rendering = false
                if (closed) return@runOnUiThread
                lastStamp = stamp
                if (html != null) {
                    val y = web.scrollY
                    web.loadDataWithBaseURL("https://preview.local/", html, "text/html", "UTF-8", null)
                    web.postDelayed({ web.scrollTo(0, y) }, 250)
                    statusView.text = getString(R.string.preview_live)
                } else {
                    statusView.text = getString(R.string.preview_error)
                    web.loadDataWithBaseURL(null,
                        "<body style='font-family:sans-serif;padding:16px'><pre>" +
                            android.text.TextUtils.htmlEncode(err ?: "?") + "</pre></body>",
                        "text/html", "UTF-8", null)
                }
            }
        }
    }

    override fun onResume() { super.onResume(); handler.removeCallbacks(poller); handler.postDelayed(poller, POLL_MS) }
    override fun onPause() { handler.removeCallbacks(poller); super.onPause() }
    override fun onDestroy() {
        closed = true
        handler.removeCallbacks(poller)
        worker.shutdownNow()
        web.destroy()
        super.onDestroy()
    }

    private fun dp(v: Int) = (v * resources.displayMetrics.density).toInt()
}
