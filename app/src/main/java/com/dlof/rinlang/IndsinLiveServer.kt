package com.dlof.rinlang

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.SystemClock
import android.util.Log
import android.widget.Toast
import org.json.JSONArray
import org.json.JSONObject
import java.io.BufferedInputStream
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.FileInputStream
import java.io.IOException
import java.io.InputStream
import java.io.OutputStream
import java.io.RandomAccessFile
import java.net.HttpURLConnection
import java.net.InetAddress
import java.net.ServerSocket
import java.net.Socket
import java.net.URL
import java.net.URLDecoder
import java.security.MessageDigest
import java.security.SecureRandom
import java.util.UUID
import java.util.concurrent.Callable
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

/**
 * «فتح في المتصفح» الحقيقي لمعاينة indsin: خادم HTTP محلي (127.0.0.1) يقدّم نفس واجهة المتصفح
 * وبروتوكول `tools/indsin-live` (POST /api + SSE /events + /f/ للوسائط + /shot.png) لكن فوق **جلسات
 * المحرّك الأصلي نفسه** داخل التطبيق (lexer ← parser ← Warp ← Fabric ← Indsin ← Dye عبر JNI) — لا محاكاة.
 *
 * ما يجعله «حقيقياً بالكامل»:
 *  - جلسة محرّك مستقلة لكل تبويب متصفح؛ اللمس/الضغط المطوّل/النقر المزدوج/hover/الإدخال/ساعة tick تصل المحرّك فعلاً.
 *  - الكتابة في محرر التطبيق تظهر في المتصفح لحظياً قبل الحفظ ([pushSource]) مع بقاء حالة Warp؛ وأي حفظ لملف
 *    من المشروع (أو مستورَد) يُحدّث التبويبات (مراقب ملفات).
 *  - التنقّل بين صفحات المشروع (`navigate:`) والرجوع، وتغيير حجم النافذة دون فقد الحالة.
 *  - الوسائط: Image/Video/Audio تُخدَّم من جذر المشروع (مع Range)، و`pickMedia` يرفع الملف المختار فعلاً إلى
 *    `<المشروع>/media/`، و`uploadMedia` ينفَّذ فعلاً من التطبيق (multipart، تقدّم حقيقي، إعادة محاولة، إلغاء).
 *  - لقطة PNG بالرسّام الأصلي، مفتّش العناصر/التدقيق/المخطط/الإحصاءات.
 *
 * الأمان: يستمع على loopback فقط؛ كل طلب يحتاج مفتاحاً عشوائياً (ترويسة X-Indsin-Key أو ?k=)؛ التحقق من Host
 * (DNS-rebinding)؛ CSP على الصفحة؛ الملفات المخدومة من داخل جذر المشروع ولأنواع الوسائط فقط (لا `..`/روابط هاربة).
 *
 * التزامن: كل نداء على جلسة أصلية يمرّ عبر [IndsinPreviewManager.runOnWorker] — خيط المعاينة الوحيد نفسه — فلا
 * يتسابق مع جلسة اللوحة المدمجة (سجلات المحرّك عامة على مستوى العملية).
 */
object IndsinLiveServer {

    const val DEFAULT_PORT = 5757
    private const val MAX_PORT = 5777
    private const val TAG = "IndsinLiveServer"
    private const val MAX_BODY = 8 * 1024 * 1024
    private const val MAX_MEDIA_BYTES = 256L * 1024 * 1024
    private const val CLIENT_ASSET = "indsin_live/index.html"
    private const val IDLE_CLIENT_MS = 15 * 60 * 1000L

    // ------------------------------------------------------------------ الحالة

    private class Doc(var text: String, var overridden: Boolean = false)

    private class Client(val cid: String) {
        var sess: RinEngine.IndsinSession? = null
        var page = ""
        val stack = ArrayList<String>()
        var width = 390
        var vp = 844
        var bg = ""
        var applied = ""
        @Volatile var seen = SystemClock.elapsedRealtime()
        @Volatile var gen = 0
        val qLock = java.lang.Object()
        val q = ArrayDeque<Pair<Boolean, String>>()
    }

    @Volatile private var server: ServerSocket? = null
    @Volatile private var appContext: Context? = null
    @Volatile private var root: File? = null
    @Volatile private var entry: String = "main.rin"
    @Volatile private var clientHtml: ByteArray? = null
    private val key: String = ByteArray(16).also { SecureRandom().nextBytes(it) }.joinToString("") { "%02x".format(it) }
    private val docs = HashMap<String, Doc>()                    // خيط المعاينة فقط
    private val clients = ConcurrentHashMap<String, Client>()
    private val uploading = ConcurrentHashMap.newKeySet<String>()            // "cid:cell" الجارية
    private val connections = ConcurrentHashMap<String, HttpURLConnection>()  // اتصالها الحالي (للإلغاء)
    private val cancelled = java.util.Collections.synchronizedSet(HashSet<String>())
    private val pool = Executors.newCachedThreadPool { r -> Thread(r, "indsin-live-server").apply { isDaemon = true } }

    val isRunning: Boolean get() = server?.isClosed == false
    val port: Int get() = server?.localPort ?: DEFAULT_PORT
    private fun baseUrl() = "http://127.0.0.1:$port/"

    /** رابط يفتح [page] (نسبة لجذر المشروع) مع المفتاح. */
    fun urlFor(page: String): String =
        baseUrl() + "?k=" + key + (if (page.isNotBlank()) "&page=" + Uri.encode(page) else "")

    // ------------------------------------------------------------------ واجهة التطبيق

    /**
     * يشغّل الخادم (إن لزم) ويفتح [page] من [projectDir] في متصفح الجهاز. [liveSource] نص المحرر الحالي إن لم يُحفَظ بعد
     * (يظهر في المتصفح كما هو، ثم تتابع الكتابة تحديثه لحظياً).
     */
    fun openInBrowser(context: Context, projectDir: File?, page: String, liveSource: String?) {
        if (!start(context)) {
            Toast.makeText(context, context.getString(R.string.indsin_browser_failed), Toast.LENGTH_LONG).show()
            return
        }
        var rel = page.replace('\\', '/').trimStart('/')
        val dir: File = projectDir ?: run {
            // ملف خارجي بلا مشروع: نص المعاينة الحالي في مجلد مؤقت خاص بهذا الاسم (كل فتح يستبدله).
            val staged = File(context.cacheDir, "indsin_browser").apply { mkdirs() }
            rel = rel.substringAfterLast('/').ifBlank { "main.indsin" }
            if (liveSource != null) try { File(staged, rel).writeText(liveSource) } catch (_: Throwable) {}
            staged
        }
        configure(dir, rel, liveSource)
        val url = urlFor(rel)
        Toast.makeText(context, context.getString(R.string.indsin_browser_opening, "127.0.0.1:$port"), Toast.LENGTH_SHORT).show()
        try {
            val i = Intent(Intent.ACTION_VIEW, Uri.parse(url))
            if (context !is android.app.Activity) i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            context.startActivity(i)
        } catch (t: Throwable) {
            Toast.makeText(context, context.getString(R.string.indsin_browser_no_app), Toast.LENGTH_LONG).show()
        }
    }

    /** يربط الخادم بمشروع/صفحة (ويستبدل الجلسات القديمة إن تغيّر الجذر). يُنفَّذ على خيط المعاينة. */
    private fun configure(projectDir: File, page: String, liveSource: String?) {
        val canon = try { projectDir.canonicalFile } catch (_: Throwable) { projectDir }
        // لا حجب لخيط الواجهة: الطابور FIFO، فطلبات المتصفح اللاحقة تُنفَّذ بعد هذا الضبط حتماً.
        IndsinPreviewManager.postToWorker {
            if (root?.path != canon.path) {
                clients.values.forEach { it.sess?.close(); it.sess = null }
                clients.clear(); docs.clear()
                root = canon
            }
            entry = page.ifBlank { entry }
            if (liveSource != null && IndsinViewTracer.containsView(liveSource)) {
                val d = loadDoc(entry, reload = true)
                if (d != null && d.text != liveSource) { d.text = liveSource; d.overridden = true }
            } else loadDoc(entry, reload = true)
        }
    }

    /**
     * نص المحرر الحالي (قبل الحفظ) إلى كل تبويب يعرض [page] — حالة Warp باقية. [page] نسبة لجذر المشروع.
     * لا يفعل شيئاً إن لم يكن الخادم يعمل أو لم تُفتح أي صفحة بعد.
     */
    fun pushSource(page: String?, text: String) {
        if (!isRunning || page.isNullOrBlank()) return
        IndsinPreviewManager.postToWorker {
            if (root == null) return@postToWorker
            val resolved = resolvePage(entry, page) ?: return@postToWorker
            val d = loadDoc(resolved) ?: return@postToWorker
            if (d.text == text) return@postToWorker
            d.text = text; d.overridden = true
            for (c in clients.values) if (c.page == resolved) {
                pushQ(c, true, wrapFrame(c, "edit", hotApply(c, text), true))
            }
        }
    }

    @Synchronized
    fun start(context: Context): Boolean {
        if (isRunning) return true
        appContext = context.applicationContext
        val bound: ServerSocket? = try {
            pool.submit(Callable<ServerSocket?> {
                var found: ServerSocket? = null
                var p = DEFAULT_PORT
                while (found == null && p <= MAX_PORT) {
                    try { found = ServerSocket(p, 50, InetAddress.getByName("127.0.0.1")) } catch (_: Exception) { p++ }
                }
                found
            }).get(5, TimeUnit.SECONDS)
        } catch (_: Throwable) { null }
        if (bound == null) return false
        server = bound
        pool.execute {
            while (!bound.isClosed) {
                try {
                    val s = bound.accept()
                    pool.execute { handle(s) }
                } catch (_: Exception) { if (bound.isClosed) break }
            }
        }
        pool.execute { watcherLoop(bound) }
        return true
    }

    @Synchronized
    fun stop() {
        try { server?.close() } catch (_: Exception) {}
        server = null
        IndsinPreviewManager.postToWorker {
            clients.values.forEach { it.sess?.close(); it.sess = null }
            clients.clear(); docs.clear()
        }
    }

    // ------------------------------------------------------------------ الوثائق والصفحات

    private fun readText(f: File): String? = try {
        var t = f.readText(Charsets.UTF_8)
        if (t.startsWith("﻿")) t = t.substring(1)
        t
    } catch (_: Throwable) { null }

    /** خيط المعاينة فقط. */
    private fun loadDoc(page: String, reload: Boolean = false): Doc? {
        val cached = docs[page]
        if (cached != null && !reload) return cached
        val r = root ?: return null
        val text = readText(File(r, page)) ?: return null
        val d = Doc(text)
        docs[page] = d
        return d
    }

    /** يحلّ [target] نسبة إلى صفحة [from] داخل الجذر فقط؛ null إن خرج عنه أو لم يوجد أو ليس .rin/.indsin. */
    private fun resolvePage(from: String, targetRaw: String): String? {
        val target = targetRaw.trim()
        val r = root ?: return null
        if (target.isEmpty() || target.contains('\u0000') || target.contains(':') || target.startsWith("\\")) return null
        val rootCanon = try { r.canonicalFile } catch (_: Throwable) { return null }
        val base = from.substringBeforeLast('/', "")
        val rel = if (target.startsWith("/")) target.substring(1) else (if (base.isEmpty()) target else "$base/$target")
        val cands = ArrayList<String>().apply {
            add(rel)
            if (!rel.substringAfterLast('/').contains('.')) { add("$rel.rin"); add("$rel.indsin") }
        }
        for (c in cands) {
            val full = try { File(rootCanon, c).canonicalFile } catch (_: Throwable) { continue }
            if (!full.path.startsWith(rootCanon.path + File.separator)) continue
            val ext = full.extension.lowercase()
            if (ext != "rin" && ext != "indsin") continue
            if (full.isFile) return full.relativeTo(rootCanon).path.replace(File.separatorChar, '/')
        }
        return null
    }

    // ------------------------------------------------------------------ بروتوكول الإطارات

    private fun q(s: String) = JSONObject.quote(s)

    private fun wrapFrame(c: Client, reason: String, env: String, withSrc: Boolean): String {
        val sb = StringBuilder()
        sb.append("{\"reason\":").append(q(reason)).append(",\"page\":").append(q(c.page)).append(",\"depth\":").append(c.stack.size)
        if (withSrc) sb.append(",\"src\":").append(q(docs[c.page]?.text ?: ""))
        sb.append(",\"env\":").append(if (env.isBlank()) "null" else env).append('}')
        return sb.toString()
    }

    private fun errJson(m: String) = "{\"ok\":false,\"error\":${q(m)}}"

    private fun pushQ(c: Client, coalesce: Boolean, data: String) {
        synchronized(c.qLock) {
            if (coalesce && c.q.isNotEmpty() && c.q.last().first) c.q[c.q.size - 1] = true to data else c.q.addLast(coalesce to data)
            while (c.q.size > 16) c.q.removeFirst()
            c.qLock.notifyAll()
        }
    }

    private fun applyViewport(c: Client) {
        val s = c.sess ?: return
        s.resize(c.width, c.vp)
        if (c.bg.isNotEmpty()) s.setDefaultBackground(c.bg)
    }

    /** يُنشئ جلسة المحرّك (من جديد: حالة Warp تبدأ صفراً) لصفحة العميل الحالية. */
    private fun startSession(c: Client): String {
        c.sess?.close(); c.sess = null
        val d = loadDoc(c.page) ?: return errJson("cannot read ${c.page}")
        val s = RinEngine.IndsinSession(d.text, c.width)
        if (s.isClosed) return errJson("could not create session")
        c.sess = s
        c.applied = d.text
        s.setViewport(c.vp)
        if (c.bg.isNotEmpty()) s.setDefaultBackground(c.bg)
        return s.currentJson()
    }

    /** يدمج نصاً جديداً في الجلسة الحيّة مع بقاء Warp؛ خطأ التحليل يُبقي آخر إطار سليم. */
    private fun hotApply(c: Client, text: String): String {
        val s = c.sess ?: return startSession(c)
        c.applied = text
        return s.updateSource(text)
    }

    private fun findClient(cid: String): Client? = clients[cid]?.also { it.seen = SystemClock.elapsedRealtime() }

    private fun clampW(v: Double, d: Int) = (if (v.isNaN()) d else v.toInt()).coerceIn(120, 4096)
    private fun clampH(v: Double, d: Int) = (if (v.isNaN()) d else v.toInt()).coerceIn(100, 8192)

    /** نداء /api — يعمل على خيط المعاينة. يعيد (حالة HTTP, JSON). */
    private fun handleApi(q: JSONObject): Pair<Int, String> {
        val cmd = q.optString("cmd")
        val r = root ?: return 409 to errJson("no project opened from the app")

        if (cmd == "source") { // دفع نص محرر خارجي بلا حفظ
            val page = if (q.optString("page").isNotEmpty()) resolvePage(entry, q.optString("page")) else entry
            if (page == null) return 404 to errJson("page not found")
            val d = loadDoc(page) ?: return 404 to errJson("cannot read page")
            val text = q.optString("text")
            if (text == d.text) return 200 to "{\"ok\":true,\"changed\":false}"
            d.text = text; d.overridden = true
            var n = 0
            for (c in clients.values) if (c.page == page) { n++; pushQ(c, true, wrapFrame(c, "edit", hotApply(c, text), true)) }
            return 200 to "{\"ok\":true,\"changed\":true,\"clients\":$n}"
        }
        if (cmd == "info") return 200 to "{\"ok\":true,\"entry\":${q(entry)},\"root\":${q(r.name)},\"clients\":${clients.size}}"

        val cid = q.optString("cid")
        if (cid.isEmpty() || cid.length > 64) return 400 to errJson("missing cid")

        if (cmd == "open") {
            val c = findClient(cid) ?: Client(cid).also { clients[cid] = it }
            c.width = clampW(q.optDouble("width", Double.NaN), 390)
            c.vp = clampH(q.optDouble("vp", Double.NaN), 844)
            c.bg = q.optString("bg")
            val wanted = q.optString("page")
            c.page = (if (wanted.isNotEmpty()) resolvePage(entry, wanted) else null) ?: entry
            c.stack.clear()
            // الصفحة الرئيسية المعدَّلة في المحرر (غير محفوظة) تُبقى؛ غيرها يُقرأ من القرص.
            if (docs[c.page]?.overridden != true) loadDoc(c.page, reload = true) else loadDoc(c.page)
            return 200 to wrapFrame(c, "open", startSession(c), true)
        }
        val c = findClient(cid) ?: return 410 to errJson("no-session")
        val s = c.sess ?: return 410 to errJson("no-session")
        val x = q.optDouble("x", 0.0)
        val y = q.optDouble("y", 0.0)

        return when (cmd) {
            "tap" -> 200 to wrapFrame(c, "tap", s.tap(x, y), false)
            "long_press" -> 200 to wrapFrame(c, "tap", s.longPress(x, y), false)
            "double_tap" -> 200 to wrapFrame(c, "tap", s.doubleTap(x, y), false)
            "hover" -> 200 to wrapFrame(c, "tap", s.hover(x, y, q.optBoolean("enter", true)), false)
            "tick" -> 200 to wrapFrame(c, "tick", s.tick(), false)
            "input" -> 200 to wrapFrame(c, "tap", s.setInput(q.optString("name"), q.optString("text")), false)
            "render" -> 200 to wrapFrame(c, "render", s.currentJson(), true)
            "resize" -> {
                c.width = clampW(q.optDouble("width", c.width.toDouble()), c.width)
                c.vp = clampH(q.optDouble("vp", c.vp.toDouble()), c.vp)
                applyViewport(c)
                200 to wrapFrame(c, "resize", s.currentJson(), false)
            }
            "bg" -> {
                c.bg = q.optString("color")
                if (c.bg.isNotEmpty() && !s.setDefaultBackground(c.bg)) { c.bg = ""; 400 to errJson("bad color") }
                else { if (c.bg.isEmpty()) s.setDefaultBackground(""); 200 to wrapFrame(c, "resize", s.currentJson(), false) }
            }
            "reload" -> { loadDoc(c.page, reload = true); 200 to wrapFrame(c, "reload", startSession(c), true) }
            "navigate", "replace" -> {
                val page = resolvePage(c.page, q.optString("target")) ?: return 404 to errJson("page not found: ${q.optString("target")}")
                if (loadDoc(page, reload = true) == null) return 404 to errJson("cannot read $page")
                if (cmd == "navigate") c.stack.add(c.page)
                c.page = page
                200 to wrapFrame(c, "navigate", startSession(c), true)
            }
            "back" -> {
                if (c.stack.isEmpty()) return 409 to errJson("no history")
                c.page = c.stack.removeAt(c.stack.size - 1)
                loadDoc(c.page)
                200 to wrapFrame(c, "navigate", startSession(c), true)
            }
            "inspect" -> {
                val what = q.optString("what", "audit")
                val body = when (what) {
                    "stats" -> s.statsJson()
                    "outline" -> q(s.outline())
                    else -> s.auditJson()
                }
                200 to "{\"ok\":true,\"what\":${q(what)},\"data\":${body.ifBlank { "null" }}}"
            }
            "media_picked" -> 200 to wrapFrame(c, "tap", s.mediaPicked(
                q.optString("cell"), (q.optJSONArray("items") ?: JSONArray()).toString(), q.optString("kind", "any"),
                q.optBoolean("multiple"), q.optDouble("maxMb", 0.0), q.optBoolean("append")), false)
            "media_progress" -> 200 to wrapFrame(c, "tap", s.mediaProgress(
                q.optString("cell"), q.optString("status"), q.optDouble("progress", 0.0), q.optString("detail")), false)
            "media_upload" -> { startUpload(c, q.optJSONObject("media") ?: JSONObject()); 200 to "{\"ok\":true}" }
            "media_cancel" -> { cancelUpload(c, q.optString("cell")); 200 to "{\"ok\":true}" }
            else -> 400 to errJson("unknown cmd: $cmd")
        }
    }

    private fun runApi(q: JSONObject): Pair<Int, String> =
        try { IndsinPreviewManager.runOnWorker { handleApi(q) } }
        catch (t: Throwable) { 500 to errJson(t.message ?: t.javaClass.simpleName) }

    // ------------------------------------------------------------------ وسائط: رفع الاختيار إلى المشروع، ورفع حقيقي إلى خادم

    private fun safeName(raw: String): String {
        val base = raw.substringAfterLast('/').substringAfterLast('\\')
        val cleaned = base.replace(Regex("[^\\p{L}\\p{N}._-]"), "_").trim('.', '_').ifBlank { "file" }
        if (cleaned.length <= 80) return cleaned
        val ext = cleaned.substringAfterLast('.', "")
        return if (ext.isNotEmpty() && ext.length <= 8) cleaned.take(70) + "." + ext else cleaned.take(80)
    }

    /** POST /m?name=… (جسم خام): يحفظ ملف المتصفح المختار في `<المشروع>/media/` باسم فريد ويعيد عنصر الوسائط الذي يفهمه المحرّك. */
    private fun saveMedia(req: Request, out: OutputStream) {
        val r = root
        if (r == null) { json(out, 409, errJson("no project")); return }
        val name = safeName(req.qs["name"] ?: "file")
        val mime = (req.qs["mime"] ?: "application/octet-stream").take(100)
        val dir = File(r, "media").apply { mkdirs() }
        val ext = name.substringAfterLast('.', "").let { if (it.isNotEmpty()) ".$it" else "" }
        val stem = name.removeSuffix(ext).ifBlank { "file" }
        val dest = File(dir, "${stem}_${UUID.randomUUID().toString().take(8)}$ext")
        try {
            dest.writeBytes(req.body)
        } catch (t: Throwable) { json(out, 500, errJson(t.message ?: "write failed")); return }
        val item = JSONObject().put("path", "media/" + dest.name).put("name", name).put("mime", mime)
            .put("size", dest.length()).put("kind", mime.substringBefore('/').ifBlank { "file" })
        json(out, 200, JSONObject().put("ok", true).put("item", item).toString())
    }

    private class PermanentUploadException(msg: String) : IOException(msg)

    private fun startUpload(c: Client, media: JSONObject) {
        val cell = media.optString("cell")
        if (cell.isBlank()) return
        val id = c.cid + ":" + cell
        if (!uploading.add(id)) return
        val url = media.optString("url")
        val field = media.optString("field", "file").ifBlank { "file" }
        val items = media.optJSONArray("items") ?: JSONArray()
        val auth = media.optString("auth", "")
        val retries = media.optInt("retries", 2).coerceIn(0, 5)
        pool.execute {
            try {
                var attempt = 1
                var detail = ""
                var failure: String? = null
                while (true) {
                    if (cancelled.contains(id)) { failure = "cancelled"; break }
                    if (attempt > 1) reportProgress(c, cell, "uploading", 0.0, attempt.toString())
                    try { detail = multipart(c, id, cell, url, field, items, auth); break }
                    catch (e: PermanentUploadException) { failure = e.message ?: "upload failed"; break }
                    catch (e: IOException) {
                        if (cancelled.contains(id)) { failure = "cancelled"; break }
                        if (attempt > retries) { failure = if (retries > 0) "failed after ${retries + 1} attempts: ${e.message}" else (e.message ?: "upload failed"); break }
                        var waited = 0L
                        val delay = minOf(8000L, 1000L shl (attempt - 1))
                        while (waited < delay && !cancelled.contains(id)) { Thread.sleep(200); waited += 200 }
                        attempt++
                    }
                }
                // الإلغاء: المحرّك أعاد الحالة إلى picked فلا نكتب فوقها.
                if (failure == null) reportProgress(c, cell, "done", 100.0, detail)
                else if (!cancelled.contains(id)) reportProgress(c, cell, "error", 0.0, failure)
            } catch (t: Throwable) {
                if (!cancelled.contains(id)) reportProgress(c, cell, "error", 0.0, t.message ?: t.toString())
            } finally {
                cancelled.remove(id); uploading.remove(id); connections.remove(id)
            }
        }
    }

    private fun cancelUpload(c: Client, cell: String) {
        val id = c.cid + ":" + cell
        if (!uploading.contains(id)) return
        cancelled.add(id)
        try { connections[id]?.disconnect() } catch (_: Throwable) {}
    }

    /** تقدّم الرفع إلى المحرّك (على خيط المعاينة) ثم الإطار الناتج إلى المتصفح عبر SSE. */
    private fun reportProgress(c: Client, cell: String, status: String, progress: Double, detail: String) {
        IndsinPreviewManager.postToWorker {
            val s = c.sess ?: return@postToWorker
            pushQ(c, status == "uploading", wrapFrame(c, "tap", s.mediaProgress(cell, status, progress, detail), false))
        }
    }

    private fun multipart(c: Client, id: String, cell: String, urlStr: String, field: String, items: JSONArray, auth: String): String {
        val lower = urlStr.lowercase()
        if (!lower.startsWith("https://") && !lower.startsWith("http://")) throw PermanentUploadException("upload URL must start with http(s)")
        val rootCanon = (root ?: throw PermanentUploadException("no project")).canonicalFile
        class Part(val file: File, val name: String, val mime: String)
        val parts = ArrayList<Part>()
        for (i in 0 until items.length()) {
            val it = items.getJSONObject(i)
            val f = File(rootCanon, it.optString("path")).canonicalFile
            if (!f.path.startsWith(rootCanon.path + File.separator) || !f.isFile) throw PermanentUploadException("file not found: ${it.optString("name")}")
            parts.add(Part(f, it.optString("name", f.name), it.optString("mime", "application/octet-stream")))
        }
        if (parts.isEmpty()) throw PermanentUploadException("no files to upload")

        val boundary = "----IndsinMedia" + UUID.randomUUID().toString().replace("-", "")
        val crlf = "\r\n"
        fun esc(s: String) = s.replace("\"", "%22").replace("\r", "").replace("\n", "")
        val heads = parts.map {
            ("--$boundary$crlf" + "Content-Disposition: form-data; name=\"${esc(field)}\"; filename=\"${esc(it.name)}\"$crlf" +
                "Content-Type: ${it.mime}$crlf$crlf").toByteArray(Charsets.UTF_8)
        }
        val tail = "--$boundary--$crlf".toByteArray(Charsets.UTF_8)
        val crlfBytes = crlf.toByteArray()
        var totalLen = tail.size.toLong()
        parts.forEachIndexed { i, p -> totalLen += heads[i].size + p.file.length() + crlfBytes.size }

        val conn = URL(urlStr).openConnection() as HttpURLConnection
        connections[id] = conn
        try {
            conn.requestMethod = "POST"; conn.doOutput = true; conn.doInput = true
            conn.instanceFollowRedirects = false // لا تسريب ملفات إلى مضيف آخر عبر تحويل
            conn.connectTimeout = 30_000; conn.readTimeout = 120_000
            conn.setRequestProperty("Content-Type", "multipart/form-data; boundary=$boundary")
            conn.setRequestProperty("Accept", "application/json, text/plain, */*")
            if (auth.isNotBlank()) conn.setRequestProperty("Authorization", auth)
            conn.setFixedLengthStreamingMode(totalLen)
            var sent = 0L; var lastPct = -1; var lastAt = 0L
            fun report() {
                if (cancelled.contains(id)) throw IOException("cancelled")
                val pct = if (totalLen > 0) (sent * 100 / totalLen).toInt().coerceIn(0, 99) else 0
                val now = System.currentTimeMillis()
                if (pct != lastPct && now - lastAt >= 150) { lastPct = pct; lastAt = now; reportProgress(c, cell, "uploading", pct.toDouble(), "") }
            }
            conn.outputStream.use { out ->
                val buf = ByteArray(32 * 1024)
                parts.forEachIndexed { i, p ->
                    out.write(heads[i]); sent += heads[i].size
                    FileInputStream(p.file).use { ins ->
                        while (true) { val n = ins.read(buf); if (n < 0) break; out.write(buf, 0, n); sent += n; report() }
                    }
                    out.write(crlfBytes); sent += crlfBytes.size
                }
                out.write(tail); sent += tail.size
            }
            val code = conn.responseCode
            val stream = if (code in 200..299) conn.inputStream else (conn.errorStream ?: conn.inputStream)
            val body = stream?.use { s ->
                val bos = ByteArrayOutputStream(); val b = ByteArray(4096)
                while (bos.size() < 4096) { val n = s.read(b); if (n < 0) break; bos.write(b, 0, n) }
                bos.toString("UTF-8").take(4096)
            } ?: ""
            if (code !in 200..299) {
                val msg = "server replied $code" + if (body.isNotBlank()) ": ${body.take(200)}" else ""
                if (code >= 500 || code == 408 || code == 429) throw IOException(msg) else throw PermanentUploadException(msg)
            }
            return body
        } finally { conn.disconnect() }
    }

    // ------------------------------------------------------------------ مراقب الملفات (استقصاء mtime/size)

    private fun isSourceExt(f: File): Boolean = when (f.extension.lowercase()) {
        "rin", "indsin", "zpp", "rinz", "json", "toml", "xml" -> true
        else -> false
    }

    private fun scan(rootDir: File): Map<String, Pair<Long, Long>> {
        val out = HashMap<String, Pair<Long, Long>>()
        fun walk(d: File, depth: Int) {
            if (out.size >= 4000) return
            val list = d.listFiles() ?: return
            for (f in list) {
                if (f.isDirectory) {
                    val n = f.name
                    if (depth < 4 && !n.startsWith(".") && n != "node_modules" && n != "build" && n != "dist") walk(f, depth + 1)
                } else if (isSourceExt(f)) {
                    out[f.relativeTo(rootDir).path.replace(File.separatorChar, '/')] = f.lastModified() to f.length()
                }
            }
        }
        walk(rootDir, 0)
        return out
    }

    private fun watcherLoop(bound: ServerSocket) {
        var tracked: Map<String, Pair<Long, Long>> = emptyMap()
        var first = true
        var lastGc = SystemClock.elapsedRealtime()
        while (!bound.isClosed) {
            try { Thread.sleep(400) } catch (_: InterruptedException) { return }
            val now = SystemClock.elapsedRealtime()
            if (now - lastGc > 30_000) { lastGc = now; gcClients(now) }
            val r = root
            if (r == null || clients.isEmpty()) { first = true; continue } // لا مبرّر لمسح القرص بلا تبويبات
            val cur = try { scan(r) } catch (_: Throwable) { continue }
            val changed = ArrayList<String>()
            if (!first) {
                for ((k, v) in cur) if (tracked[k] != v) changed.add(k)
                for (k in tracked.keys) if (!cur.containsKey(k)) changed.add(k)
            }
            first = false
            tracked = cur
            if (changed.isEmpty()) continue
            IndsinPreviewManager.postToWorker { applyDiskChanges(changed) }
        }
    }

    private fun applyDiskChanges(changed: List<String>) {
        val r = root ?: return
        for (ch in changed) {
            val d = docs[ch] ?: continue
            val t = readText(File(r, ch)) ?: continue
            d.text = t; d.overridden = false // القرص أحدث من أي نص محرر دُفع سابقاً
        }
        for (c in clients.values) {
            val d = loadDoc(c.page) ?: continue
            val pageChanged = changed.contains(c.page)
            if (pageChanged && d.text == c.applied) continue // حفظ لنفس ما دُفع حيّاً
            pushQ(c, true, wrapFrame(c, "file", hotApply(c, d.text), true))
        }
    }

    private fun gcClients(now: Long) {
        val stale = clients.values.filter { now - it.seen > IDLE_CLIENT_MS }
        if (stale.isEmpty()) return
        IndsinPreviewManager.postToWorker {
            for (c in stale) { c.sess?.close(); c.sess = null; clients.remove(c.cid) }
        }
    }

    // ------------------------------------------------------------------ HTTP

    private class Request(val method: String, val path: String, val qs: Map<String, String>, val headers: Map<String, String>, val body: ByteArray)

    private fun handle(sock: Socket) {
        var keepOpen = false
        try {
            sock.soTimeout = 15000
            sock.tcpNoDelay = true
            val input = BufferedInputStream(sock.getInputStream())
            val out = sock.getOutputStream()
            val req = readRequest(input)
            if (req == null) { text(out, 400, "Bad Request"); return }
            keepOpen = route(req, sock, out)
            out.flush()
        } catch (t: Throwable) {
            Log.w(TAG, "request failed: ${t.message}")
        } finally {
            if (!keepOpen) try { sock.close() } catch (_: Exception) {}
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
            if (head.size() > 32 * 1024) return null
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
        val target = first[1]
        val qm = target.indexOf('?')
        val path = Uri.decode(if (qm >= 0) target.substring(0, qm) else target)
        val qs = HashMap<String, String>()
        if (qm >= 0) for (kv in target.substring(qm + 1).split('&')) {
            if (kv.isEmpty()) continue
            val e = kv.indexOf('=')
            try {
                qs[URLDecoder.decode(if (e < 0) kv else kv.substring(0, e), "UTF-8")] = if (e < 0) "" else URLDecoder.decode(kv.substring(e + 1), "UTF-8")
            } catch (_: Exception) {}
        }
        val len = headers["content-length"]?.toLongOrNull() ?: 0L
        val isMedia = first[0].uppercase() == "POST" && path == "/m"
        if (len < 0 || len > (if (isMedia) MAX_MEDIA_BYTES else MAX_BODY.toLong())) return null
        val body = ByteArray(len.toInt())
        var read = 0
        while (read < body.size) {
            val n = input.read(body, read, body.size - read)
            if (n < 0) return null
            read += n
        }
        return Request(first[0].uppercase(), path, qs, headers, body)
    }

    private fun respond(out: OutputStream, status: Int, type: String, body: ByteArray, extra: String = "", headOnly: Boolean = false) {
        val reason = when (status) {
            200 -> "OK"; 206 -> "Partial Content"; 400 -> "Bad Request"; 401 -> "Unauthorized"; 403 -> "Forbidden"
            404 -> "Not Found"; 405 -> "Method Not Allowed"; 409 -> "Conflict"; 410 -> "Gone"; 416 -> "Range Not Satisfiable"
            else -> "Error"
        }
        val sb = StringBuilder()
        sb.append("HTTP/1.1 ").append(status).append(' ').append(reason).append("\r\n")
        sb.append("Content-Type: ").append(type).append("\r\nContent-Length: ").append(body.size).append("\r\n")
        sb.append("Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\nConnection: close\r\n")
        sb.append(extra).append("\r\n")
        out.write(sb.toString().toByteArray(Charsets.ISO_8859_1))
        if (!headOnly) out.write(body)
    }

    private fun text(out: OutputStream, status: Int, msg: String) = respond(out, status, "text/plain; charset=utf-8", msg.toByteArray(Charsets.UTF_8))
    private fun json(out: OutputStream, status: Int, body: String) = respond(out, status, "application/json; charset=utf-8", body.toByteArray(Charsets.UTF_8))

    private fun hostAllowed(host: String?): Boolean {
        val h = host?.lowercase() ?: return false
        val p = port
        return h == "127.0.0.1:$p" || h == "localhost:$p" || h == "[::1]:$p"
    }

    private fun authorized(req: Request): Boolean {
        val given = req.headers["x-indsin-key"] ?: req.qs["k"] ?: return false
        return MessageDigest.isEqual(given.toByteArray(Charsets.UTF_8), key.toByteArray(Charsets.UTF_8))
    }

    private val csp = "Content-Security-Policy: default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; " +
        "img-src 'self' https: data: blob:; media-src 'self' https: blob:; frame-src https: data:; connect-src 'self'; " +
        "base-uri 'none'; form-action 'none'\r\n"

    /** يعيد true إن كان المقبس سيُغلقه المسار نفسه (لا يُستعمل حالياً: SSE يُغلق عند الانتهاء داخل serveEvents). */
    private fun route(req: Request, sock: Socket, out: OutputStream): Boolean {
        if (!hostAllowed(req.headers["host"]) || req.headers["sec-fetch-site"] == "cross-site") { text(out, 403, "Forbidden"); return false }

        if ((req.path == "/" || req.path == "/index.html") && (req.method == "GET" || req.method == "HEAD")) {
            if (!authorized(req)) { text(out, 401, "Unauthorized — open the link from the app (it contains the key)."); return false }
            val html = clientHtml ?: try {
                appContext!!.assets.open(CLIENT_ASSET).use { it.readBytes() }.also { clientHtml = it }
            } catch (t: Throwable) { text(out, 500, "client asset missing: ${t.message}"); return false }
            respond(out, 200, "text/html; charset=utf-8", html, csp, headOnly = req.method == "HEAD")
            return false
        }
        if (!authorized(req)) { json(out, 401, errJson("unauthorized")); return false }

        when {
            req.path == "/api" && req.method == "POST" -> {
                val q = try { JSONObject(String(req.body, Charsets.UTF_8)) } catch (_: Throwable) { null }
                if (q == null) { json(out, 400, errJson("bad json")); return false }
                val (st, body) = runApi(q)
                json(out, st, body)
            }
            req.path == "/m" && req.method == "POST" -> saveMedia(req, out)
            req.path == "/events" && req.method == "GET" -> serveEvents(req, sock, out)
            req.path == "/shot.png" && req.method == "GET" -> serveShot(req, out)
            req.path.startsWith("/f/") && (req.method == "GET" || req.method == "HEAD") -> serveProjectFile(req, out)
            else -> text(out, 404, "not found")
        }
        return false
    }

    // ---- SSE ---------------------------------------------------------------------------------

    private fun serveEvents(req: Request, sock: Socket, out: OutputStream) {
        val c = clients[req.qs["cid"] ?: ""]
        if (c == null) { text(out, 410, "no-session"); return }
        val myGen: Int
        synchronized(c.qLock) { myGen = ++c.gen; c.qLock.notifyAll() }
        sock.soTimeout = 0
        out.write(("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream; charset=utf-8\r\nCache-Control: no-store\r\n" +
            "X-Accel-Buffering: no\r\nConnection: keep-alive\r\n\r\nretry: 1000\n\n").toByteArray())
        out.flush()
        while (isRunning) {
            val chunk = StringBuilder()
            synchronized(c.qLock) {
                if (c.q.isEmpty() && c.gen == myGen) try { c.qLock.wait(12_000) } catch (_: InterruptedException) { return }
                if (c.gen != myGen) return // تبويب أعاد الاتصال: هذا الاتصال استُبدل
                if (c.q.isEmpty()) chunk.append(": ping\n\n")
                else while (c.q.isNotEmpty()) chunk.append("data: ").append(c.q.removeFirst().second).append("\n\n")
            }
            c.seen = SystemClock.elapsedRealtime()
            try { out.write(chunk.toString().toByteArray(Charsets.UTF_8)); out.flush() } catch (_: IOException) { return }
        }
    }

    // ---- لقطة PNG ------------------------------------------------------------------------------

    private fun serveShot(req: Request, out: OutputStream) {
        val cid = req.qs["cid"] ?: ""
        val tmp = File(appContext!!.cacheDir, "indsin_live_${UUID.randomUUID().toString().take(8)}.png")
        val png: ByteArray? = try {
            IndsinPreviewManager.runOnWorker {
                val s = findClient(cid)?.sess
                if (s != null && s.exportPng(tmp.absolutePath)) tmp.readBytes() else null
            }
        } catch (_: Throwable) { null } finally { tmp.delete() }
        if (png == null || png.isEmpty()) json(out, 409, errJson("no valid frame (parse error?)"))
        else respond(out, 200, "image/png", png, "Content-Disposition: inline; filename=\"indsin-preview.png\"\r\n")
    }

    // ---- ملفات المشروع (وسائط فقط، مع Range) -----------------------------------------------------

    private fun mimeFor(ext: String): Pair<String, Boolean>? = when (ext) {
        "png" -> "image/png" to false; "jpg", "jpeg" -> "image/jpeg" to false; "gif" -> "image/gif" to false
        "webp" -> "image/webp" to false; "avif" -> "image/avif" to false; "bmp" -> "image/bmp" to false; "ico" -> "image/x-icon" to false
        "svg" -> "image/svg+xml" to true; "pdf" -> "application/pdf" to true
        "mp4" -> "video/mp4" to false; "webm" -> "video/webm" to false; "ogv" -> "video/ogg" to false; "mov" -> "video/quicktime" to false
        "mp3" -> "audio/mpeg" to false; "wav" -> "audio/wav" to false; "ogg" -> "audio/ogg" to false; "m4a" -> "audio/mp4" to false
        "aac" -> "audio/aac" to false; "flac" -> "audio/flac" to false
        "woff2" -> "font/woff2" to false; "woff" -> "font/woff" to false; "ttf" -> "font/ttf" to false; "otf" -> "font/otf" to false
        else -> null
    }

    private fun serveProjectFile(req: Request, out: OutputStream) {
        val rel = req.path.removePrefix("/f/")
        val r = root
        if (r == null || rel.isEmpty() || rel.contains('\u0000') || rel.split('/').any { it == ".." }) { text(out, 403, "forbidden"); return }
        val rootCanon = try { r.canonicalFile } catch (_: Throwable) { text(out, 403, "forbidden"); return }
        val full = try { File(rootCanon, rel).canonicalFile } catch (_: Throwable) { text(out, 403, "forbidden"); return }
        if (!full.path.startsWith(rootCanon.path + File.separator)) { text(out, 403, "forbidden"); return }
        val (mime, sandbox) = mimeFor(full.extension.lowercase()) ?: run { text(out, 403, "this file type is not served"); return }
        if (!full.isFile) { text(out, 404, "not found"); return }
        val size = full.length()
        var from = 0L
        var to = if (size > 0) size - 1 else 0L
        var code = 200
        val range = req.headers["range"]
        if (range != null && range.startsWith("bytes=")) {
            val spec = range.substring(6)
            val dash = spec.indexOf('-')
            if (dash >= 0) {
                val a = spec.substring(0, dash); val b = spec.substring(dash + 1)
                if (a.isEmpty()) { val n = b.toLongOrNull() ?: 0L; from = if (n > size) 0 else size - n }
                else { from = a.toLongOrNull() ?: 0L; if (b.isNotEmpty()) to = minOf(b.toLongOrNull() ?: to, size - 1) }
                if (from >= size) { respond(out, 416, "text/plain", ByteArray(0), "Content-Range: bytes */$size\r\n"); return }
                code = 206
            }
        }
        val len = if (size > 0) to - from + 1 else 0L
        val head = StringBuilder()
        head.append("HTTP/1.1 ").append(code).append(if (code == 206) " Partial Content" else " OK").append("\r\n")
        head.append("Content-Type: ").append(mime).append("\r\nContent-Length: ").append(len).append("\r\nAccept-Ranges: bytes\r\nCache-Control: no-cache\r\n")
        head.append("X-Content-Type-Options: nosniff\r\n")
        if (sandbox) head.append("Content-Security-Policy: sandbox\r\n")
        if (code == 206) head.append("Content-Range: bytes $from-$to/$size\r\n")
        head.append("Connection: close\r\n\r\n")
        out.write(head.toString().toByteArray(Charsets.ISO_8859_1))
        if (req.method == "HEAD" || len == 0L) return
        RandomAccessFile(full, "r").use { raf ->
            raf.seek(from)
            val buf = ByteArray(64 * 1024)
            var left = len
            while (left > 0) {
                val n = raf.read(buf, 0, minOf(buf.size.toLong(), left).toInt())
                if (n <= 0) break
                out.write(buf, 0, n); left -= n
            }
        }
    }
}
