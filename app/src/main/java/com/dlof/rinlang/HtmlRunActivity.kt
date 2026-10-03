package com.dlof.rinlang

import android.annotation.SuppressLint
import android.content.Intent
import android.graphics.Color
import android.net.Uri
import android.os.Bundle
import android.util.TypedValue
import android.view.Gravity
import android.view.View
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
 * يشغّل مشروع HTML: يعرض index.html داخل WebView ويربطه بـ container.rin عبر جلسة Rin حيّة
 * ([RinEngine.HtmlSession]).
 *
 *   index.html     الواجهة (HTML)
 *   style.css      التنسيق — يُدمَج تلقائياً في <style> عند وجود <link rel="stylesheet" href="style.css">
 *   container.rin  المنطق: متغيّرات عامة (let/warp) ودوال علوية تُستدعى من الصفحة
 *
 * الربط من داخل index.html بلا أي JavaScript (نفس توجيهات web/rinhtml/rinhtml.js):
 *   rin-click="add()"        يستدعي دالة Rin عند النقر (وسائط: أرقام / "نص" / true|false / اسم متغيّر)
 *   rin-text="count"         يعرض قيمة متغيّر Rin كنص
 *   rin-show="visible"       يُظهر العنصر/يُخفيه حسب قيمة متغيّر Rin
 *   rin-model="name"         ربط ثنائي الاتجاه لحقل إدخال (input/textarea/checkbox)
 * ومن JavaScript عند الحاجة: window.rin.call("fn", a, b) / rin.get("x") / rin.set("x", v).
 */
class HtmlRunActivity : AppCompatActivity() {

    companion object {
        const val EXTRA_PROJECT_NAME = "extra_project_name"
        const val ENTRY_HTML = "index.html"
        const val ENTRY_LOGIC = "container.rin"

        /** يفتح المشروع [projectName] في شاشة التشغيل. */
        fun start(context: android.content.Context, projectName: String) {
            context.startActivity(Intent(context, HtmlRunActivity::class.java)
                .putExtra(EXTRA_PROJECT_NAME, projectName))
        }

        /** هل هذا المشروع قابل للتشغيل كصفحة HTML (فيه index.html)؟ */
        fun isHtmlProject(project: Project): Boolean = File(project.dir, ENTRY_HTML).isFile

        /** سكربت الربط المحقون في الصفحة. يتكلّم مع الجلسة الحية عبر واجهة RinBridge. */
        private val RUNTIME_JS = """
(function () {
  if (window.rin) return;
  var G = {};
  try { G = JSON.parse(RinBridge.globals()); } catch (e) {}

  function parseCall(expr) {
    var m = /^\s*([A-Za-z_][A-Za-z0-9_]*)\s*\(([^)]*)\)\s*$/.exec(expr || '');
    if (!m) return { name: (expr || '').trim(), args: [] };
    var raw = m[2].trim();
    if (!raw) return { name: m[1], args: [] };
    var args = raw.split(',').map(function (t) {
      t = t.trim();
      if (/^-?\d+(\.\d+)?$/.test(t)) return parseFloat(t);
      if (/^".*"${'$'}/.test(t) || /^'.*'${'$'}/.test(t)) return t.slice(1, -1);
      if (t === 'true') return true;
      if (t === 'false') return false;
      if (Object.prototype.hasOwnProperty.call(G, t)) return G[t];
      return t;
    });
    return { name: m[1], args: args };
  }

  function render() {
    document.querySelectorAll('[rin-text]').forEach(function (el) {
      var v = G[el.getAttribute('rin-text')];
      el.textContent = (v === undefined || v === null) ? '' : String(v);
    });
    document.querySelectorAll('[rin-show]').forEach(function (el) {
      el.style.display = G[el.getAttribute('rin-show')] ? '' : 'none';
    });
    document.querySelectorAll('[rin-model]').forEach(function (el) {
      if (document.activeElement === el) return;
      var v = G[el.getAttribute('rin-model')];
      if (el.type === 'checkbox') el.checked = !!v;
      else el.value = (v === undefined || v === null) ? '' : String(v);
    });
  }

  function call(fn) {
    var args = Array.prototype.slice.call(arguments, 1);
    var res;
    try { res = JSON.parse(RinBridge.call(fn, JSON.stringify(args))); }
    catch (e) { res = { ok: false, error: String(e), globals: G }; }
    if (res.globals) G = res.globals;
    if (!res.ok) RinBridge.error(fn + '(): ' + res.error);
    render();
    return res;
  }

  function set(name, value) {
    G[name] = value;
    RinBridge.set(name, JSON.stringify(value));
    render();
  }

  document.addEventListener('click', function (ev) {
    var el = ev.target.closest ? ev.target.closest('[rin-click]') : null;
    if (!el) return;
    var c = parseCall(el.getAttribute('rin-click'));
    call.apply(null, [c.name].concat(c.args));
  });
  document.addEventListener('input', function (ev) {
    var el = ev.target;
    if (!el || !el.getAttribute || !el.hasAttribute('rin-model')) return;
    var v = el.type === 'checkbox' ? el.checked : el.type === 'number' ? parseFloat(el.value) : el.value;
    set(el.getAttribute('rin-model'), v);
  });

  window.rin = {
    call: call, set: set,
    get: function (n) { return G[n]; },
    globals: function () { return G; },
    render: render
  };
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', render);
  else render();
})();
"""
    }

    private lateinit var project: Project
    private lateinit var web: WebView
    private lateinit var consoleScroll: ScrollView
    private lateinit var consoleText: TextView
    private var session: RinEngine.HtmlSession? = null
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

        val logic = File(project.dir, ENTRY_LOGIC).takeIf { it.isFile } ?: File(project.dir, "main.rin")
        try {
            val s = RinEngine.HtmlSession.create(logic.readText(), project.dir.absolutePath)
            session = s
            if (s.bootOutput.isNotBlank()) log(s.bootOutput.trimEnd())
        } catch (t: Throwable) {
            showFatal(getString(R.string.html_run_rin_error, t.message ?: ""))
            return
        }

        web.removeJavascriptInterface("RinBridge")
        web.addJavascriptInterface(Bridge(), "RinBridge")
        val page = buildPage(htmlFile.readText())
        web.loadDataWithBaseURL(Uri.fromFile(project.dir).toString() + "/", page, "text/html", "UTF-8", null)
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
                out = if (out.contains("</head>", true)) out.replaceFirst(Regex("</head>", RegexOption.IGNORE_CASE), tag + "</head>")
                else tag + out
            }
        }
        val script = "<script>$RUNTIME_JS</script>"
        return if (out.contains("</body>", true))
            out.replaceFirst(Regex("</body>", RegexOption.IGNORE_CASE), script + "</body>")
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
        @JavascriptInterface fun call(fn: String, argsJson: String): String =
            session?.call(fn, argsJson) ?: "{\"ok\":false,\"error\":\"no session\",\"globals\":{}}"
        @JavascriptInterface fun set(name: String, valueJson: String) { session?.setGlobal(name, valueJson) }
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
