package com.dlof.rinlang

import android.app.Activity
import android.app.AlertDialog
import android.content.ClipboardManager
import android.content.Context
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.os.Handler
import android.os.Looper
import android.text.InputType
import android.view.View
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.RadioButton
import android.widget.RadioGroup
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.atomic.AtomicBoolean

/*
 * استرداد مستودعات (GitHub / GitLab / Bitbucket / Codeberg-Gitea-Forgejo / رابط ZIP مباشر):
 *
 *   • showImportDialog : من شاشة «المشاريع» — يُنشئ مشروعاً جديداً من المستودع.
 *   • showIntoProject  : من شاشة «الملفات» — يسترد المستودع (أو مجلداً منه) داخل المجلد الحالي بثلاث طرق دمج.
 *   • updateFromSource : يعيد جلب نفس المصدر المحفوظ (.rin_repo.json) ويدمجه، مع إمكان تغيير الفرع.
 *
 * الأمان: https فقط؛ الرمز السرّي اختياري ولا يُحفظ؛ لا يُرسَل إلا إلى مضيف الطلب الأول (يُسقَط عند التحويل لمضيف
 * آخر كخوادم التخزين)؛ فكّ آمن ضد Zip Slip وحدود للحجم؛ وكل ما يُستبدل أو يُزال يُنسخ إلى .rin_backup/ أولاً.
 */
object RepoRestore {

    private const val SOURCE_FILE = ".rin_repo.json"
    private const val BACKUP_DIR = ".rin_backup"
    private const val MAX_REDIRECTS = 5

    // =============================================================================================
    // نقاط الدخول
    // =============================================================================================

    /** مشروع جديد من مستودع. [onDone] يستلم المشروع المُنشأ. */
    fun showImportDialog(activity: Activity, onDone: (Project) -> Unit) {
        showDialog(
            activity, activity.getString(R.string.repo_title_new),
            project = null, relDir = "", prefill = null, onNew = onDone, onMerged = null
        )
    }

    /** استرداد داخل [relDir] من [project]. */
    fun showIntoProject(activity: Activity, project: Project, relDir: String, onDone: () -> Unit) {
        showDialog(
            activity, activity.getString(R.string.repo_title_into),
            project = project, relDir = relDir, prefill = null, onNew = null, onMerged = onDone
        )
    }

    /** يعيد جلب المصدر المحفوظ للمجلد الحالي (أو أقرب مجلد أب له مصدر). */
    fun updateFromSource(activity: Activity, project: Project, relDir: String, onDone: () -> Unit) {
        val found = findSource(project, relDir)
        if (found == null) {
            Toast.makeText(activity, R.string.repo_no_source, Toast.LENGTH_LONG).show()
            return
        }
        showDialog(
            activity, activity.getString(R.string.repo_title_update),
            project = project, relDir = found.relDir, prefill = found, onNew = null, onMerged = onDone
        )
    }

    // =============================================================================================
    // الحوار
    // =============================================================================================

    private class Form(
        val root: View,
        val url: EditText,
        val ref: EditText,
        val pickRef: Button,
        val token: EditText,
        val name: EditText?,
        val modes: RadioGroup?
    )

    private fun buildForm(activity: Activity, withName: Boolean, withModes: Boolean, prefill: Source?): Form {
        val d = activity.resources.displayMetrics.density
        fun px(v: Int) = (v * d).toInt()
        fun ltrField(hint: String): EditText = EditText(activity).apply {
            this.hint = hint
            setSingleLine(true)
            textDirection = View.TEXT_DIRECTION_LTR
        }

        val url = ltrField(activity.getString(R.string.repo_url_hint)).apply {
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_URI
        }
        val ref = ltrField(activity.getString(R.string.repo_ref_hint))
        val pick = Button(activity).apply { text = activity.getString(R.string.repo_ref_pick) }
        val token = ltrField(activity.getString(R.string.repo_token_hint)).apply {
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_PASSWORD
        }
        val name = if (withName) ltrField(activity.getString(R.string.repo_name_hint)) else null

        if (prefill != null) {
            url.setText(prefill.display)
            ref.setText(prefill.spec.ref)
        } else {
            clipboardRepoUrl(activity)?.let { url.setText(it) }
        }

        val refRow = LinearLayout(activity).apply {
            orientation = LinearLayout.HORIZONTAL
            addView(ref, LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f))
            addView(pick, LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT))
        }

        val box = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(px(22), px(12), px(22), px(4))
            addView(url)
            addView(refRow)
            addView(token)
            if (name != null) addView(name)
        }

        var group: RadioGroup? = null
        if (withModes) {
            box.addView(TextView(activity).apply {
                text = activity.getString(R.string.repo_mode_title)
                textSize = 13f
                setPadding(0, px(14), 0, px(4))
            })
            val g = RadioGroup(activity).apply { orientation = RadioGroup.VERTICAL }
            val wanted = prefill?.mode ?: MergeMode.OVERWRITE
            fun radio(label: Int, m: MergeMode) {
                val rb = RadioButton(activity).apply {
                    id = View.generateViewId()
                    text = activity.getString(label)
                    tag = m
                    isChecked = (m == wanted)
                }
                g.addView(rb)
            }
            radio(R.string.repo_mode_add, MergeMode.ADD_ONLY)
            radio(R.string.repo_mode_overwrite, MergeMode.OVERWRITE)
            radio(R.string.repo_mode_mirror, MergeMode.MIRROR)
            box.addView(g)
            group = g
        }

        val scroll = ScrollView(activity).apply { addView(box) }
        return Form(scroll, url, ref, pick, token, name, group)
    }

    private fun selectedMode(form: Form): MergeMode {
        val g = form.modes ?: return MergeMode.OVERWRITE
        val rb = g.findViewById<RadioButton>(g.checkedRadioButtonId)
        return (rb?.tag as? MergeMode) ?: MergeMode.OVERWRITE
    }

    private fun showDialog(
        activity: Activity, title: String,
        project: Project?, relDir: String, prefill: Source?,
        onNew: ((Project) -> Unit)?, onMerged: (() -> Unit)?
    ) {
        val form = buildForm(activity, withName = project == null, withModes = project != null, prefill = prefill)
        form.pickRef.setOnClickListener { pickRef(activity, form) }

        val dlg = AlertDialog.Builder(activity)
            .setTitle(title)
            .setView(form.root)
            .setPositiveButton(R.string.repo_start, null)
            .setNegativeButton(R.string.cancel, null)
            .create()
        dlg.setOnShowListener {
            dlg.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener {
                val parsed = RepoParser.parse(form.url.text.toString())
                if (parsed == null) {
                    Toast.makeText(activity, R.string.repo_invalid, Toast.LENGTH_LONG).show()
                    return@setOnClickListener
                }
                val refText = form.ref.text.toString().trim()
                if (!RepoParser.isValidRef(refText)) {
                    Toast.makeText(activity, R.string.repo_invalid_ref, Toast.LENGTH_LONG).show()
                    return@setOnClickListener
                }
                val spec: RepoSpec = if (refText.isNotEmpty()) parsed.copy(ref = refText) else parsed
                if (!isOnline(activity)) {
                    Toast.makeText(activity, R.string.dl_url_offline, Toast.LENGTH_LONG).show()
                    return@setOnClickListener
                }
                val mode = selectedMode(form)
                val req = Request(
                    spec = spec, token = form.token.text.toString().trim(),
                    newName = form.name?.text?.toString()?.trim().orEmpty(),
                    project = project, relDir = relDir, mode = mode, onNew = onNew, onMerged = onMerged
                )
                dlg.dismiss()
                if (project != null && mode == MergeMode.MIRROR) {
                    AlertDialog.Builder(activity)
                        .setMessage(R.string.repo_mirror_confirm)
                        .setPositiveButton(R.string.repo_start) { _, _ -> run(activity, req) }
                        .setNegativeButton(R.string.cancel, null)
                        .show()
                } else {
                    run(activity, req)
                }
            }
        }
        dlg.show()
    }

    /** يقترح الرابط من الحافظة إن كان مستودعاً مدعوماً. */
    private fun clipboardRepoUrl(ctx: Context): String? = try {
        val cm = ctx.getSystemService(Context.CLIPBOARD_SERVICE) as? ClipboardManager
        val text = cm?.primaryClip?.takeIf { it.itemCount > 0 }?.getItemAt(0)?.coerceToText(ctx)?.toString()?.trim()
        if (!text.isNullOrEmpty() && text.length < 300 && RepoParser.parse(text) != null) text else null
    } catch (_: Throwable) { null }

    // =============================================================================================
    // اختيار فرع/وسم
    // =============================================================================================

    private fun pickRef(activity: Activity, form: Form) {
        val spec = RepoParser.parse(form.url.text.toString())
        if (spec == null) {
            Toast.makeText(activity, R.string.repo_invalid, Toast.LENGTH_LONG).show()
            return
        }
        if (spec.provider == RepoProvider.DIRECT) {
            Toast.makeText(activity, R.string.repo_refs_unsupported, Toast.LENGTH_LONG).show()
            return
        }
        if (!isOnline(activity)) {
            Toast.makeText(activity, R.string.dl_url_offline, Toast.LENGTH_LONG).show()
            return
        }
        val token = form.token.text.toString().trim()
        Toast.makeText(activity, R.string.repo_refs_loading, Toast.LENGTH_SHORT).show()
        val main = Handler(Looper.getMainLooper())
        Thread {
            try {
                val refs = RepoNet.listRefs(spec, token)
                main.post {
                    if (refs.isEmpty()) {
                        Toast.makeText(activity, R.string.repo_refs_none, Toast.LENGTH_LONG).show()
                    } else {
                        val branch = activity.getString(R.string.repo_kind_branch)
                        val tag = activity.getString(R.string.repo_kind_tag)
                        val labels = refs.map { (if (it.isTag) tag else branch) + " · " + it.name }.toTypedArray()
                        AlertDialog.Builder(activity)
                            .setTitle(R.string.repo_ref_pick)
                            .setItems(labels) { _, i -> form.ref.setText(refs[i].name) }
                            .setNegativeButton(R.string.cancel, null)
                            .show()
                    }
                }
            } catch (t: Throwable) {
                main.post { Toast.makeText(activity, describe(activity, t, token.isNotEmpty()), Toast.LENGTH_LONG).show() }
            }
        }.start()
    }

    // =============================================================================================
    // التنفيذ
    // =============================================================================================

    private class Request(
        val spec: RepoSpec,
        val token: String,
        val newName: String,
        val project: Project?,
        val relDir: String,
        val mode: MergeMode,
        val onNew: ((Project) -> Unit)?,
        val onMerged: (() -> Unit)?
    )

    private fun run(activity: Activity, req: Request) {
        val main = Handler(Looper.getMainLooper())
        val cancelled = AtomicBoolean(false)
        val progress = RinDownloadProgressDialog(
            activity, req.spec.displayName(),
            activity.getString(R.string.repo_progress_title),
            activity.getString(R.string.repo_progress_status)
        )
        progress.setCancelAction(activity.getString(R.string.cancel)) { cancelled.set(true) }
        progress.show()

        Thread {
            var temp: File? = null
            try {
                val ctx = activity.applicationContext
                var spec = req.spec
                if (spec.provider == RepoProvider.GITEA && spec.ref.isBlank()) {
                    spec = spec.copy(ref = RepoNet.giteaDefaultBranch(spec, req.token))
                }
                val tmp: File = if (req.project == null) {
                    ProjectManager.newImportTempDir(ctx)
                } else {
                    File(ctx.cacheDir, "repo_${System.currentTimeMillis()}").also { it.mkdirs() }
                }
                temp = tmp
                val tree = File(tmp, "tree")

                val extracted = RepoNet.download(spec, req.token, tree, cancelled) { read, total ->
                    main.post { progress.updateProgress(read, total) }
                }
                if (extracted.files == 0) throw RepoMessage(activity.getString(R.string.repo_err_empty))
                main.post { progress.setStatus(activity.getString(R.string.repo_progress_install)) }

                RepoArchive.stripSingleTopFolder(tree)
                val content = RepoArchive.narrow(tree, spec.subPath)
                    ?: throw RepoMessage(activity.getString(R.string.repo_err_subpath, spec.subPath))
                val fileCount = RepoArchive.countFiles(content)
                if (fileCount == 0) throw RepoMessage(activity.getString(R.string.repo_err_empty))

                if (req.project == null) {
                    val wanted = req.newName.ifBlank { spec.repoName }
                    val project = ProjectManager.installImportedTree(ctx, content, wanted, guessType(content))
                    writeSource(project.dir, spec, MergeMode.OVERWRITE)
                    main.post {
                        progress.finishSuccess(activity.getString(R.string.download_status_done))
                        Toast.makeText(activity, activity.getString(R.string.repo_done_new, project.name, fileCount), Toast.LENGTH_LONG).show()
                        main.postDelayed({ progress.dismiss(); req.onNew?.invoke(project) }, 450)
                    }
                } else {
                    val dest = ProjectManager.resolveDir(req.project, req.relDir)
                    val stamp = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(Date())
                    val backupRoot = File(req.project.dir, "$BACKUP_DIR/restore_$stamp")
                    val report = RepoMerge.merge(content, dest, req.mode, backupRoot) { rel ->
                        rel.startsWith(".rin_") || (req.relDir.isBlank() && rel == RinProjectMeta.FILE_NAME)
                    }
                    writeSource(dest, spec, req.mode)
                    main.post {
                        progress.finishSuccess(activity.getString(R.string.download_status_done))
                        Toast.makeText(
                            activity,
                            activity.getString(
                                R.string.repo_done_merge,
                                report.added, report.updated, report.unchanged, report.skipped, report.removed
                            ),
                            Toast.LENGTH_LONG
                        ).show()
                        main.postDelayed({ progress.dismiss(); req.onMerged?.invoke() }, 450)
                    }
                }
            } catch (c: RepoCancelled) {
                main.post { progress.dismiss() }
            } catch (t: Throwable) {
                main.post {
                    progress.finishError(describe(activity, t, req.token.isNotEmpty()))
                    main.postDelayed({ progress.dismiss() }, 2200)
                }
            } finally {
                temp?.deleteRecursively()
            }
        }.start()
    }

    /** نوع المشروع من محتواه: main.rin ← حر، index.html (+script.js) ← HTML/HTML_JS. */
    private fun guessType(dir: File): ProjectType = when {
        File(dir, "main.rin").isFile -> ProjectType.FREE
        File(dir, "index.html").isFile ->
            if (File(dir, "script.js").isFile) ProjectType.HTML_JS else ProjectType.HTML
        else -> ProjectType.FREE
    }

    private fun describe(activity: Activity, t: Throwable, hadToken: Boolean): String = when (t) {
        is RepoMessage -> t.message.orEmpty()
        is RepoTooBig -> activity.getString(R.string.repo_err_too_big)
        is RepoHttpException -> when (t.code) {
            404 -> activity.getString(if (hadToken) R.string.repo_err_404_token else R.string.repo_err_404)
            401, 403 -> activity.getString(R.string.repo_err_auth)
            429 -> activity.getString(R.string.repo_err_rate)
            else -> activity.getString(R.string.repo_err_http, t.code)
        }
        else -> activity.getString(R.string.download_error_toast, t.message ?: t.javaClass.simpleName)
    }

    private fun isOnline(ctx: Context): Boolean {
        val cm = ctx.getSystemService(Context.CONNECTIVITY_SERVICE) as? ConnectivityManager ?: return true
        val caps = cm.getNetworkCapabilities(cm.activeNetwork) ?: return false
        return caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
    }

    // =============================================================================================
    // سجلّ المصدر (.rin_repo.json) — بلا أي رمز سرّي
    // =============================================================================================

    private class Source(val spec: RepoSpec, val mode: MergeMode, val relDir: String, val display: String)

    private fun writeSource(dir: File, spec: RepoSpec, mode: MergeMode) {
        try {
            val o = JSONObject()
            o.put("v", 1)
            o.put("provider", spec.provider.name)
            o.put("host", spec.host)
            o.put("path", spec.path)
            o.put("ref", spec.ref)
            o.put("subPath", spec.subPath)
            o.put("directUrl", spec.directUrl)
            o.put("mode", mode.name)
            o.put("fetchedAt", SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss'Z'", Locale.US)
                .apply { timeZone = java.util.TimeZone.getTimeZone("UTC") }.format(Date()))
            File(dir, SOURCE_FILE).writeText(o.toString(2), Charsets.UTF_8)
        } catch (_: Throwable) {
            // السجلّ مساعد فقط؛ فشله لا يُفشل الاسترداد.
        }
    }

    private fun readSource(dir: File, relDir: String): Source? {
        val f = File(dir, SOURCE_FILE)
        if (!f.isFile) return null
        return try {
            val o = JSONObject(f.readText(Charsets.UTF_8))
            val provider = RepoProvider.valueOf(o.getString("provider"))
            val spec = RepoSpec(
                provider, o.getString("host"), o.getString("path"),
                o.optString("ref"), o.optString("subPath"), o.optString("directUrl")
            )
            val mode = try { MergeMode.valueOf(o.optString("mode", "OVERWRITE")) } catch (_: Throwable) { MergeMode.OVERWRITE }
            val shown = if (provider == RepoProvider.DIRECT) spec.directUrl else "https://" + spec.host + "/" + spec.path
            Source(spec, mode, relDir, shown)
        } catch (_: Throwable) { null }
    }

    /** يبحث عن سجلّ المصدر في [relDir] ثم في آبائه حتى جذر المشروع. */
    private fun findSource(project: Project, relDir: String): Source? {
        val parts = relDir.split('/').filter { it.isNotBlank() }
        for (n in parts.size downTo 0) {
            val rel = parts.take(n).joinToString("/")
            readSource(ProjectManager.resolveDir(project, rel), rel)?.let { return it }
        }
        return null
    }

    // =============================================================================================
    // الشبكة
    // =============================================================================================

    private object RepoNet {

        class Ref(val name: String, val isTag: Boolean)

        /** يتبع التحويلات يدوياً (https فقط) ولا يرسل رؤوس المصادقة إلا لمضيف الطلب الأول. */
        fun connect(startUrl: String, headers: Map<String, String>, accept: String): HttpURLConnection {
            var current = URL(startUrl)
            if (current.protocol != "https") throw IOException("https only")
            val origin = current.host.lowercase()
            for (attempt in 0..MAX_REDIRECTS) {
                val target = current
                val conn = (target.openConnection() as HttpURLConnection).apply {
                    instanceFollowRedirects = false
                    connectTimeout = 20_000
                    readTimeout = 30_000
                    setRequestProperty("User-Agent", "RinStudio")
                    setRequestProperty("Accept", accept)
                    if (target.host.equals(origin, ignoreCase = true)) {
                        for ((k, v) in headers) setRequestProperty(k, v)
                    }
                }
                val code = conn.responseCode
                if (code in 300..399) {
                    val loc = conn.getHeaderField("Location")
                    conn.disconnect()
                    if (loc == null) throw RepoHttpException(code)
                    val next = URL(target, loc)
                    if (next.protocol != "https") throw IOException("https only")
                    current = next
                    continue
                }
                return conn
            }
            throw IOException("too many redirects")
        }

        fun download(
            spec: RepoSpec, token: String, dest: File, cancelled: AtomicBoolean,
            onProgress: (Long, Long) -> Unit
        ): RepoArchive.Extracted {
            val conn = connect(
                RepoLinks.archiveUrl(spec, token.isNotEmpty()),
                RepoLinks.authHeaders(spec, token),
                "application/zip,application/octet-stream,*/*"
            )
            try {
                if (conn.responseCode !in 200..299) throw RepoHttpException(conn.responseCode)
                val limits = RepoLimits()
                val total = conn.contentLengthLong
                if (total > limits.maxTotalBytes) throw RepoTooBig()
                var last = 0L
                val input = CountingInputStream(conn.inputStream) { n ->
                    val now = System.currentTimeMillis()
                    if (now - last >= 80) {
                        last = now
                        onProgress(n, if (total > 0) total else n)
                    }
                }
                return RepoArchive.extractZip(input, dest, limits) { cancelled.get() }
            } finally {
                conn.disconnect()
            }
        }

        private fun httpText(url: String, headers: Map<String, String>): String {
            val conn = connect(url, headers, "application/json")
            try {
                if (conn.responseCode !in 200..299) throw RepoHttpException(conn.responseCode)
                val bytes = conn.inputStream.use { it.readBytes() }
                if (bytes.size > 4 * 1024 * 1024) throw RepoTooBig()
                return String(bytes, Charsets.UTF_8)
            } finally {
                conn.disconnect()
            }
        }

        private fun names(a: JSONArray): List<String> {
            val out = ArrayList<String>()
            for (i in 0 until a.length()) {
                val n = a.optJSONObject(i)?.optString("name").orEmpty()
                if (n.isNotBlank() && RepoParser.isValidRef(n)) out.add(n)
            }
            return out
        }

        private fun parseNames(text: String, key: String?): List<String> =
            if (key == null) names(JSONArray(text))
            else names(JSONObject(text).optJSONArray(key) ?: JSONArray())

        fun listRefs(spec: RepoSpec, token: String): List<Ref> {
            val api = RepoLinks.refsApi(spec) ?: return emptyList()
            val headers = RepoLinks.authHeaders(spec, token)
            val out = ArrayList<Ref>()
            for (b in parseNames(httpText(api.branches, headers), api.arrayKey)) out.add(Ref(b, false))
            try {
                for (t in parseNames(httpText(api.tags, headers), api.arrayKey)) out.add(Ref(t, true))
            } catch (_: Throwable) {
                // الوسوم اختيارية: نكتفي بالفروع إن فشل جلبها.
            }
            return out
        }

        fun giteaDefaultBranch(spec: RepoSpec, token: String): String {
            val text = httpText(RepoLinks.giteaRepoInfoUrl(spec), RepoLinks.authHeaders(spec, token))
            val b = JSONObject(text).optString("default_branch")
            return if (b.isNotBlank() && RepoParser.isValidRef(b)) b else "main"
        }
    }
}
