package com.dlof.rinlang

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.text.TextUtils
import android.util.Log
import android.widget.Toast
import org.json.JSONObject
import java.io.BufferedInputStream
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.InputStream
import java.io.OutputStream
import java.net.InetAddress
import java.net.ServerSocket
import java.net.Socket
import java.util.UUID
import java.util.concurrent.Callable
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

/**
 * خادم HTTP محلي صغير على http://localhost:7700/ لمعاينة مشاريع HTML وRin في متصفح الجهاز.
 *
 *   /                      قائمة المشاريع
 *   /<مشروع>/              مشروع HTML: index.html مدمجاً فيه style.css وسكربتاته المحلية، ومربوطاً بجلسة
 *                          Rin حيّة عبر مسارات /__rin/ إن وُجد ملف منطق Rin. مشروع HTML + JS يعمل كصفحة عادية.
 *                          أي مشروع Rin آخر: صفحة تشغيل (زر ▶) تعرض مخرجات الملف المختار.
 *   /<مشروع>/ملف           ملفات المشروع الثابتة (الصور، data.json ...). الملفات المخفية (.rin_state) محجوبة.
 *
 * الأمان: يستمع على 127.0.0.1 فقط؛ يرفض أي Host غير localhost/127.0.0.1/[::1] (حماية DNS rebinding)
 * وأي طلب Sec-Fetch-Site: cross-site؛ وكل طلب POST (استدعاء Rin / تشغيل ملف) يتطلب رمزاً عشوائياً لا يعرفه
 * إلا من حصل على الصفحة من الخادم نفسه (ترويسة X-Rin-Token)، فلا تستطيع صفحة ويب غريبة تشغيل كودك.
 */
object RinLocalServer {

    const val DEFAULT_PORT = 7700
    private const val MAX_PORT = 7710
    private const val TAG = "RinLocalServer"
    private const val MAX_BODY = 2 * 1024 * 1024

    @Volatile private var server: ServerSocket? = null
    @Volatile private var appContext: Context? = null
    private val token: String = UUID.randomUUID().toString().replace("-", "")
    private val runtimes = ConcurrentHashMap<String, HtmlProjectRuntime>()
    private val engineLock = Any()
    private val pool = Executors.newCachedThreadPool { r -> Thread(r, "rin-local-server").apply { isDaemon = true } }

    val isRunning: Boolean get() = server?.isClosed == false
    val port: Int get() = server?.localPort ?: DEFAULT_PORT
    fun baseUrl(): String = "http://localhost:$port/"
    fun projectUrl(name: String): String = baseUrl() + Uri.encode(name) + "/"

    /** يشغّل الخادم إن لم يكن يعمل (يجرّب 7700 ثم حتى 7710 إن كان المنفذ مشغولاً). يعيد true عند النجاح. */
    @Synchronized
    fun start(context: Context): Boolean {
        if (isRunning) return true
        appContext = context.applicationContext
        val bound: ServerSocket? = try {
            // الربط على خيط خلفي (StrictMode يمنع عمليات الشبكة على الخيط الرئيسي).
            pool.submit(Callable<ServerSocket?> {
                var found: ServerSocket? = null
                var p = DEFAULT_PORT
                while (found == null && p <= MAX_PORT) {
                    try { found = ServerSocket(p, 50, InetAddress.getByName("127.0.0.1")) } catch (e: Exception) { p++ }
                }
                found
            }).get(5, TimeUnit.SECONDS)
        } catch (t: Throwable) { null }
        if (bound == null) return false
        server = bound
        pool.execute {
            while (!bound.isClosed) {
                try {
                    val sock = bound.accept()
                    pool.execute { handle(sock) }
                } catch (e: Exception) {
                    if (bound.isClosed) break
                }
            }
        }
        return true
    }

    @Synchronized
    fun stop() {
        try { server?.close() } catch (_: Exception) {}
        server = null
        runtimes.values.forEach { it.close() }
        runtimes.clear()
    }

    /** يشغّل الخادم (إن لزم) ويفتح [projectName] في متصفح الجهاز. */
    fun openInBrowser(context: Context, projectName: String) {
        if (!start(context)) {
            Toast.makeText(context, context.getString(R.string.local_server_failed), Toast.LENGTH_LONG).show()
            return
        }
        val url = projectUrl(projectName)
        Toast.makeText(context, context.getString(R.string.local_server_started, "localhost:$port"), Toast.LENGTH_SHORT).show()
        try {
            val i = Intent(Intent.ACTION_VIEW, Uri.parse(url))
            if (context !is Activity) i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            context.startActivity(i)
        } catch (t: Throwable) {
            Toast.makeText(context, url, Toast.LENGTH_LONG).show()
        }
    }

    // ------------------------------------------------------------------ HTTP

    private class Request(
        val method: String,
        val rawPath: String,
        val headers: Map<String, String>,
        val body: ByteArray
    )

    private fun handle(sock: Socket) {
        try {
            sock.soTimeout = 15000
            val input = BufferedInputStream(sock.getInputStream())
            val out = sock.getOutputStream()
            val req = readRequest(input)
            if (req == null) { respond(out, 400, "Bad Request", "text/plain; charset=utf-8", "Bad Request".toByteArray()); return }
            route(req, out)
            out.flush()
        } catch (t: Throwable) {
            Log.w(TAG, "request failed: ${t.message}")
        } finally {
            try { sock.close() } catch (_: Exception) {}
        }
    }

    private fun readRequest(input: InputStream): Request? {
        val head = ByteArrayOutputStream()
        var matched = 0
        val end = byteArrayOf(13, 10, 13, 10)
        while (matched < 4) {
            val b = input.read()
            if (b < 0) return null
            head.write(b)
            matched = if (b.toByte() == end[matched]) matched + 1 else if (b.toByte() == end[0]) 1 else 0
            if (head.size() > 16 * 1024) return null
        }
        val lines = head.toString("ISO-8859-1").split("\r\n").filter { it.isNotEmpty() }
        if (lines.isEmpty()) return null
        val first = lines[0].split(" ")
        if (first.size < 2) return null
        val headers = HashMap<String, String>()
        for (i in 1 until lines.size) {
            val ix = lines[i].indexOf(':')
            if (ix > 0) headers[lines[i].substring(0, ix).trim().lowercase()] = lines[i].substring(ix + 1).trim()
        }
        val len = headers["content-length"]?.toIntOrNull() ?: 0
        if (len < 0 || len > MAX_BODY) return null
        val body = ByteArray(len)
        var read = 0
        while (read < len) {
            val n = input.read(body, read, len - read)
            if (n < 0) return null
            read += n
        }
        return Request(first[0].uppercase(), first[1], headers, body)
    }

    private fun respond(
        out: OutputStream, status: Int, reason: String, type: String, body: ByteArray,
        extra: Map<String, String> = emptyMap(), headOnly: Boolean = false
    ) {
        val sb = StringBuilder()
        sb.append("HTTP/1.1 ").append(status).append(' ').append(reason).append("\r\n")
        sb.append("Content-Type: ").append(type).append("\r\n")
        sb.append("Content-Length: ").append(body.size).append("\r\n")
        sb.append("Cache-Control: no-store\r\n")
        sb.append("X-Content-Type-Options: nosniff\r\n")
        sb.append("Connection: close\r\n")
        for ((k, v) in extra) sb.append(k).append(": ").append(v).append("\r\n")
        sb.append("\r\n")
        out.write(sb.toString().toByteArray(Charsets.ISO_8859_1))
        if (!headOnly) out.write(body)
    }

    private fun html(out: OutputStream, status: Int, reason: String, page: String, headOnly: Boolean = false) =
        respond(out, status, reason, "text/html; charset=utf-8", page.toByteArray(Charsets.UTF_8), headOnly = headOnly)

    private fun json(out: OutputStream, obj: String) =
        respond(out, 200, "OK", "application/json; charset=utf-8", obj.toByteArray(Charsets.UTF_8))

    // ------------------------------------------------------------------ التوجيه

    private fun hostAllowed(host: String?): Boolean {
        if (host == null) return false
        val h = if (host.startsWith("[")) host.substringBefore("]") + "]" else host.substringBefore(':')
        return h == "localhost" || h == "127.0.0.1" || h == "[::1]"
    }

    private fun route(req: Request, out: OutputStream) {
        val ctx = appContext ?: return
        if (!hostAllowed(req.headers["host"]) || req.headers["sec-fetch-site"] == "cross-site") {
            respond(out, 403, "Forbidden", "text/plain; charset=utf-8", "Forbidden".toByteArray()); return
        }
        val isGet = req.method == "GET" || req.method == "HEAD"
        val isPost = req.method == "POST"
        if (!isGet && !isPost) { respond(out, 405, "Method Not Allowed", "text/plain; charset=utf-8", "Method Not Allowed".toByteArray()); return }
        if (isPost && req.headers["x-rin-token"] != token) {
            respond(out, 403, "Forbidden", "text/plain; charset=utf-8", "Bad token".toByteArray()); return
        }
        val head = req.method == "HEAD"
        val rawPath = req.rawPath.substringBefore('?').substringBefore('#')
        val segs = rawPath.split('/').filter { it.isNotEmpty() }.map { Uri.decode(it) }
        if (segs.any { it == ".." || it == "." || it.contains('/') || it.contains('\\') || it.contains('\u0000') }) {
            respond(out, 400, "Bad Request", "text/plain; charset=utf-8", "Bad path".toByteArray()); return
        }

        if (segs.isEmpty()) { html(out, 200, "OK", indexPage(ctx), head); return }

        val project = ProjectManager.listProjects(ctx).find { it.name == segs[0] }
        if (project == null) { html(out, 404, "Not Found", messagePage("404", "المشروع غير موجود: ${esc(segs[0])}"), head); return }

        val rest = segs.drop(1)
        if (rest.isEmpty()) {
            if (!rawPath.endsWith("/")) {
                respond(out, 301, "Moved Permanently", "text/plain; charset=utf-8", ByteArray(0), mapOf("Location" to "/" + Uri.encode(project.name) + "/"))
                return
            }
            if (!isGet) { respond(out, 405, "Method Not Allowed", "text/plain; charset=utf-8", ByteArray(0)); return }
            if (HtmlRunActivity.isHtmlProject(project)) servePage(ctx, project, out, head) else html(out, 200, "OK", runnerPage(project), head)
            return
        }

        when (rest[0]) {
            "__rin" -> { if (isPost) serveBridge(project, rest.getOrNull(1), req, out) else respond(out, 405, "Method Not Allowed", "text/plain; charset=utf-8", ByteArray(0)) }
            "__run" -> { if (isPost) serveRun(ctx, project, req, out) else respond(out, 405, "Method Not Allowed", "text/plain; charset=utf-8", ByteArray(0)) }
            "index.html" -> if (isGet && rest.size == 1 && HtmlRunActivity.isHtmlProject(project)) servePage(ctx, project, out, head) else serveStatic(project, rest, out, head)
            else -> serveStatic(project, rest, out, head)
        }
    }

    // ------------------------------------------------------------------ مشروع HTML

    private fun servePage(ctx: Context, project: Project, out: OutputStream, head: Boolean) {
        runtimes.remove(project.name)?.close()
        val rt = HtmlProjectRuntime(ctx, project, null) { Log.i(TAG, "[${project.name}] $it") }
        val error = rt.open()
        if (error != null) { html(out, 500, "Internal Server Error", messagePage("خطأ", esc(error)), head); return }
        runtimes[project.name] = rt
        val base = "/" + Uri.encode(project.name) + "/"
        val prelude = "<script>window.RIN_HTTP_BASE=" + JSONObject.quote(base) +
            ";window.RIN_HTTP_TOKEN=" + JSONObject.quote(token) + ";</script>"
        html(out, 200, "OK", rt.buildPage(prelude), head)
    }

    private fun serveBridge(project: Project, op: String?, req: Request, out: OutputStream) {
        val rt = runtimes[project.name]
        if (rt == null || rt.session == null) {
            json(out, "{\"ok\":false,\"error\":\"no session\",\"globals\":{}}"); return
        }
        val body = try { JSONObject(String(req.body, Charsets.UTF_8).ifBlank { "{}" }) } catch (t: Throwable) { JSONObject() }
        when (op) {
            "globals" -> json(out, rt.globalsJson())
            "call" -> json(out, rt.call(body.optString("fn"), body.optJSONArray("args")?.toString() ?: "[]"))
            "set" -> {
                rt.setGlobal(body.optString("name"), HtmlProjectRuntime.toJsonLiteral(if (body.has("value")) body.get("value") else null))
                json(out, "{}")
            }
            "clear" -> { rt.clearSavedState(); json(out, "{}") }
            "error" -> { Log.w(TAG, "[${project.name}] page: ${body.optString("message")}"); json(out, "{}") }
            else -> respond(out, 404, "Not Found", "text/plain; charset=utf-8", ByteArray(0))
        }
    }

    // ------------------------------------------------------------------ ملفات ثابتة

    private fun serveStatic(project: Project, rest: List<String>, out: OutputStream, head: Boolean) {
        // الملفات/المجلدات المخفية (.rin_state ...) لا تُقدَّم أبداً.
        if (rest.any { it.startsWith(".") }) { html(out, 404, "Not Found", messagePage("404", "غير موجود"), head); return }
        val root = project.dir.canonicalFile
        val f = File(project.dir, rest.joinToString("/")).canonicalFile
        if (!f.path.startsWith(root.path + File.separator) || !f.isFile) {
            html(out, 404, "Not Found", messagePage("404", "الملف غير موجود: ${esc(rest.joinToString("/"))}"), head); return
        }
        respond(out, 200, "OK", mimeOf(f.name), f.readBytes(), headOnly = head)
    }

    private fun mimeOf(name: String): String = when (name.substringAfterLast('.', "").lowercase()) {
        "html", "htm" -> "text/html; charset=utf-8"
        "css" -> "text/css; charset=utf-8"
        "js", "mjs" -> "text/javascript; charset=utf-8"
        "json", "webmanifest" -> "application/json; charset=utf-8"
        "svg" -> "image/svg+xml"
        "png" -> "image/png"
        "jpg", "jpeg" -> "image/jpeg"
        "gif" -> "image/gif"
        "webp" -> "image/webp"
        "ico" -> "image/x-icon"
        "woff" -> "font/woff"
        "woff2" -> "font/woff2"
        "ttf" -> "font/ttf"
        "otf" -> "font/otf"
        "mp3" -> "audio/mpeg"
        "mp4" -> "video/mp4"
        "webm" -> "video/webm"
        "wasm" -> "application/wasm"
        "xml" -> "application/xml; charset=utf-8"
        else -> "text/plain; charset=utf-8" // txt, md, rin, ...
    }

    // ------------------------------------------------------------------ مشروع Rin (صفحة تشغيل)

    private fun serveRun(ctx: Context, project: Project, req: Request, out: OutputStream) {
        val body = try { JSONObject(String(req.body, Charsets.UTF_8).ifBlank { "{}" }) } catch (t: Throwable) { JSONObject() }
        val rel = body.optString("file", "main.rin")
        val root = project.dir.canonicalFile
        val f = File(project.dir, rel).canonicalFile
        if (!rel.endsWith(".rin", ignoreCase = true) || !f.path.startsWith(root.path + File.separator) || !f.isFile) {
            json(out, JSONObject().put("ok", false).put("output", "").put("error", "ملف Rin غير موجود: $rel").toString()); return
        }
        val result = JSONObject()
        try {
            val source = f.readText()
            synchronized(engineLock) {
                val previous = RinEngine.currentBaseDir()
                try {
                    RinEngine.init(ctx, project.dir.absolutePath)
                    val r = RinEngine.runSourceStructured(source)
                    result.put("ok", r.success).put("output", r.output)
                    if (!r.success) result.put("error", r.diagnosticText ?: r.errorMessage ?: "خطأ")
                } finally {
                    RinEngine.init(ctx, previous)
                }
            }
        } catch (t: Throwable) {
            result.put("ok", false).put("output", "").put("error", t.message ?: "خطأ")
        }
        json(out, result.toString())
    }

    private fun runnerPage(project: Project): String {
        val files = (project.dir.listFiles { x -> x.isFile && x.name.endsWith(".rin", ignoreCase = true) } ?: emptyArray())
            .map { it.name }.sortedWith(compareBy({ !it.equals("main.rin", true) }, { it.lowercase() }))
        val options = files.joinToString("") { "<option value=\"${esc(it)}\">${esc(it)}</option>" }
        val body = """
<div class="card">
  <h1>${esc(project.name)}</h1>
  <p class="sub">مشروع Rin — اختر ملفاً ثم اضغط تشغيل</p>
  <div class="row">
    <select id="file">${options.ifEmpty { "<option>main.rin</option>" }}</select>
    <button id="run">▶ تشغيل</button>
  </div>
  <div id="status" class="status"></div>
  <pre id="out" dir="auto">لم يُشغَّل بعد.</pre>
</div>
<script>
var TOKEN = ${JSONObject.quote(token)};
document.getElementById('run').onclick = function () {
  var out = document.getElementById('out'), st = document.getElementById('status'), btn = this;
  btn.disabled = true; st.textContent = 'جارٍ التشغيل…'; st.className = 'status';
  fetch('__run', { method: 'POST', headers: { 'X-Rin-Token': TOKEN, 'Content-Type': 'application/json' },
      body: JSON.stringify({ file: document.getElementById('file').value }) })
    .then(function (r) { return r.json(); })
    .then(function (j) {
      out.textContent = (j.output || '') + (j.error ? '\n' + j.error : '') || '(لا مخرجات)';
      st.textContent = j.ok ? 'تم بنجاح' : 'انتهى بخطأ'; st.className = 'status ' + (j.ok ? 'ok' : 'err');
    })
    .catch(function (e) { st.textContent = 'تعذّر الاتصال: ' + e; st.className = 'status err'; })
    .then(function () { btn.disabled = false; });
};
</script>"""
        return shell(project.name, body)
    }

    // ------------------------------------------------------------------ صفحات الخادم

    private fun esc(s: String): String = TextUtils.htmlEncode(s)

    private fun indexPage(ctx: Context): String {
        val items = ProjectManager.listProjects(ctx).joinToString("") { p ->
            val kind = when (p.type) {
                ProjectType.HTML -> "HTML + Rin"
                ProjectType.HTML_JS -> "HTML + JS"
                else -> "Rin"
            }
            val web = HtmlRunActivity.isHtmlProject(p)
            val cls = if (web) "web" else "rin"
            "<a class=\"item\" href=\"/${Uri.encode(p.name)}/\"><span class=\"badge $cls\">$kind</span><span class=\"name\">${esc(p.name)}</span><span class=\"go\">‹</span></a>"
        }
        val body = """
<div class="card">
  <h1>Rin — localhost:$port</h1>
  <p class="sub">مشاريع HTML وRin على هذا الجهاز</p>
  <div class="list">${items.ifEmpty { "<p class=\"sub\">لا توجد مشاريع بعد.</p>" }}</div>
</div>"""
        return shell("Rin", body)
    }

    private fun messagePage(title: String, message: String): String =
        shell(title, "<div class=\"card\"><h1>${esc(title)}</h1><p class=\"sub\">$message</p><a class=\"back\" href=\"/\">← كل المشاريع</a></div>")

    private fun shell(title: String, body: String): String = """<!DOCTYPE html>
<html lang="ar" dir="rtl"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>${esc(title)}</title>
<style>
*{box-sizing:border-box}body{margin:0;min-height:100vh;display:flex;justify-content:center;padding:24px 14px;background:#0a0a10;color:#eceef7;font-family:system-ui,sans-serif}
.card{width:min(100%,560px);height:fit-content;padding:24px;border-radius:16px;background:#14141c;border:1px solid #23232f}
h1{margin:0 0 4px;font-size:20px}.sub{margin:4px 0 16px;color:#9a9ab0;font-size:13px}
.list{display:flex;flex-direction:column;gap:8px}
.item{display:flex;align-items:center;gap:12px;padding:12px 14px;border-radius:12px;background:#0f0f17;border:1px solid #23232f;color:inherit;text-decoration:none}
.item:hover{border-color:#7c5cff}.name{flex:1;font-weight:600}.go{color:#9a9ab0}
.badge{padding:3px 9px;border-radius:999px;font-size:11px;font-weight:700;color:#fff}.badge.web{background:#e44d26}.badge.rin{background:#7c5cff}
.row{display:flex;gap:8px;margin-bottom:12px}select{flex:1;min-width:0;padding:10px 12px;border-radius:10px;border:1px solid #2a2a38;background:#0a0a10;color:inherit;font-size:14px}
button{padding:10px 18px;border:none;border-radius:10px;background:#7c5cff;color:#fff;font-size:14px;font-weight:700;cursor:pointer}button:disabled{opacity:.6}
.status{min-height:18px;margin-bottom:8px;font-size:12px;color:#9a9ab0}.status.ok{color:#7fe3a1}.status.err{color:#ff8a8a}
pre{margin:0;padding:14px;border-radius:12px;background:#0a0a10;border:1px solid #23232f;white-space:pre-wrap;word-break:break-word;font:13px/1.6 ui-monospace,monospace;color:#d6d8e6;min-height:120px}
.back{display:inline-block;margin-top:8px;color:#9d86ff;text-decoration:none}
</style></head><body>$body</body></html>"""
}
