package com.dlof.rinlang

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.provider.DocumentsContract
import android.webkit.MimeTypeMap
import org.json.JSONObject
import java.io.File
import java.util.concurrent.Executors

/**
 * حفظ/مزامنة المشاريع في مجلد يختاره المستخدم على الهاتف عبر Storage Access Framework
 * (لا يحتاج أي صلاحية تخزين). المشروع الأصلي يبقى في تخزين التطبيق، والمجلد الخارجي نسخة مرآة:
 *  - [saveNew]: ينشئ مجلداً فرعياً باسم المشروع (اسم فريد) وينسخ كل الملفات (+ .rinpak اختيارياً) ويربطه بالمشروع.
 *  - [sync]: يرفع الملفات الجديدة/المعدَّلة فقط ولا يحذف شيئاً من المجلد الخارجي أبداً.
 *  - [syncAsync]: يُستدعى عند الخروج من المحرر إن كانت المزامنة التلقائية مفعّلة لهذا المشروع.
 */
object ExternalProjectSaver {
    private const val PREFS = "external_save"
    private const val KEY_TREE = "default_tree"
    private const val KEY_ENABLED = "enabled_default"
    private const val KEY_AUTO = "auto_default"
    private const val KEY_ZIP = "zip_default"
    private const val LINK = "link."
    private const val BINARY = "application/octet-stream"

    private val io = Executors.newSingleThreadExecutor()
    private val main = Handler(Looper.getMainLooper())

    class Result(val ok: Boolean, val files: Int = 0, val bytes: Long = 0, val folderName: String = "", val error: String? = null)
    enum class Check { OK, NO_PERMISSION, NOT_WRITABLE, INVALID }
    class Link(val tree: Uri, val folderId: String, val folderName: String, val auto: Boolean)
    private class Remote(val docId: String, val isDir: Boolean, val size: Long, val modified: Long)
    private class Stats { var copied = 0; var bytes = 0L }

    private fun prefs(ctx: Context) = ctx.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    // ---------------------------------------------------------------- الإعدادات الافتراضية للمعالج
    fun defaultTree(ctx: Context): Uri? = prefs(ctx).getString(KEY_TREE, null)?.let { Uri.parse(it) }
    fun setDefaultTree(ctx: Context, uri: Uri) { prefs(ctx).edit().putString(KEY_TREE, uri.toString()).apply() }
    fun defaultEnabled(ctx: Context) = prefs(ctx).getBoolean(KEY_ENABLED, false)
    fun autoDefault(ctx: Context) = prefs(ctx).getBoolean(KEY_AUTO, true)
    fun zipDefault(ctx: Context) = prefs(ctx).getBoolean(KEY_ZIP, false)
    fun rememberChoices(ctx: Context, enabled: Boolean, auto: Boolean, zip: Boolean) {
        prefs(ctx).edit().putBoolean(KEY_ENABLED, enabled).putBoolean(KEY_AUTO, auto).putBoolean(KEY_ZIP, zip).apply()
    }

    // ---------------------------------------------------------------- الصلاحية والتحقق
    fun persist(ctx: Context, tree: Uri) {
        try {
            ctx.contentResolver.takePersistableUriPermission(
                tree, Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION)
        } catch (_: Throwable) {}
    }

    fun hasPermission(ctx: Context, tree: Uri): Boolean =
        try { ctx.contentResolver.persistedUriPermissions.any { it.uri == tree && it.isWritePermission } } catch (_: Throwable) { false }

    private fun rootDoc(tree: Uri): Uri =
        DocumentsContract.buildDocumentUriUsingTree(tree, DocumentsContract.getTreeDocumentId(tree))

    /** يتحقق أن المجلد صالح ومسموح بالكتابة فيه (بإنشاء ملف تجريبي وحذفه). */
    fun check(ctx: Context, tree: Uri): Check {
        if (!DocumentsContract.isTreeUri(tree)) return Check.INVALID
        if (!hasPermission(ctx, tree)) return Check.NO_PERMISSION
        return try {
            val t = DocumentsContract.createDocument(ctx.contentResolver, rootDoc(tree), BINARY, ".rin_write_test")
            if (t == null) Check.NOT_WRITABLE else { DocumentsContract.deleteDocument(ctx.contentResolver, t); Check.OK }
        } catch (_: Throwable) { Check.NOT_WRITABLE }
    }

    /** اسم المجلد المختار للعرض. */
    fun folderLabel(ctx: Context, tree: Uri): String = try {
        ctx.contentResolver.query(rootDoc(tree), arrayOf(DocumentsContract.Document.COLUMN_DISPLAY_NAME), null, null, null)?.use {
            if (it.moveToFirst()) it.getString(0) else null
        } ?: tree.lastPathSegment.orEmpty()
    } catch (_: Throwable) { tree.lastPathSegment.orEmpty() }

    // ---------------------------------------------------------------- الربط بالمشروع
    fun linkOf(ctx: Context, projectName: String): Link? = try {
        prefs(ctx).getString(LINK + projectName, null)?.let {
            val j = JSONObject(it)
            Link(Uri.parse(j.getString("tree")), j.getString("folderId"), j.optString("folderName"), j.optBoolean("auto", true))
        }
    } catch (_: Throwable) { null }

    private fun saveLink(ctx: Context, name: String, l: Link) {
        val j = JSONObject().put("tree", l.tree.toString()).put("folderId", l.folderId).put("folderName", l.folderName).put("auto", l.auto)
        prefs(ctx).edit().putString(LINK + name, j.toString()).apply()
    }

    fun unlink(ctx: Context, projectName: String) { prefs(ctx).edit().remove(LINK + projectName).apply() }

    // ---------------------------------------------------------------- النسخ
    fun saveNew(ctx: Context, project: Project, tree: Uri, alsoZip: Boolean = false, auto: Boolean = true): Result {
        return try {
            val app = ctx.applicationContext
            val existing = children(app, tree, DocumentsContract.getTreeDocumentId(tree)).keys
            val folder = ProjectSavePlan.uniqueName(existing, project.name)
            val dirUri = DocumentsContract.createDocument(app.contentResolver, rootDoc(tree), DocumentsContract.Document.MIME_TYPE_DIR, folder)
                ?: return Result(false, error = "create folder")
            val dirId = DocumentsContract.getDocumentId(dirUri)
            val st = Stats()
            copyTree(app, tree, project.dir, dirId, "", st)
            if (alsoZip) runCatching {
                val zip = ProjectManager.exportProject(app, project)
                putFile(app, tree, dirId, zip.name, zip, st)
            }
            saveLink(app, project.name, Link(tree, dirId, folder, auto))
            Result(true, st.copied, st.bytes, folder)
        } catch (t: Throwable) { Result(false, error = t.message ?: t.javaClass.simpleName) }
    }

    /** يرفع الجديد/المعدَّل فقط إلى المجلد المربوط؛ لا حذف من الوجهة. */
    fun sync(ctx: Context, project: Project): Result {
        val app = ctx.applicationContext
        val link = linkOf(app, project.name) ?: return Result(false, error = "not linked")
        return try {
            if (!hasPermission(app, link.tree)) return Result(false, error = "permission")
            val st = Stats()
            copyTree(app, link.tree, project.dir, link.folderId, "", st)
            Result(true, st.copied, st.bytes, link.folderName)
        } catch (t: Throwable) { Result(false, error = t.message ?: t.javaClass.simpleName) }
    }

    /** مزامنة صامتة في الخلفية للمشاريع المربوطة بمزامنة تلقائية. */
    fun syncAsync(ctx: Context, project: Project, onDone: ((Result) -> Unit)? = null) {
        val app = ctx.applicationContext
        val link = linkOf(app, project.name) ?: return
        if (!link.auto) return
        io.execute {
            val r = sync(app, project)
            if (onDone != null) main.post { onDone(r) }
        }
    }

    // ---------------------------------------------------------------- داخلي (SAF)
    private fun children(ctx: Context, tree: Uri, parentId: String): Map<String, Remote> {
        val out = HashMap<String, Remote>()
        val uri = DocumentsContract.buildChildDocumentsUriUsingTree(tree, parentId)
        val cols = arrayOf(
            DocumentsContract.Document.COLUMN_DOCUMENT_ID, DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_MIME_TYPE, DocumentsContract.Document.COLUMN_SIZE,
            DocumentsContract.Document.COLUMN_LAST_MODIFIED)
        ctx.contentResolver.query(uri, cols, null, null, null)?.use { c ->
            while (c.moveToNext()) {
                val isDir = c.getString(2) == DocumentsContract.Document.MIME_TYPE_DIR
                out[c.getString(1)] = Remote(c.getString(0), isDir, if (c.isNull(3)) -1L else c.getLong(3), if (c.isNull(4)) 0L else c.getLong(4))
            }
        }
        return out
    }

    private fun mimeFor(name: String): String =
        MimeTypeMap.getSingleton().getMimeTypeFromExtension(name.substringAfterLast('.', "").lowercase()) ?: BINARY

    private fun copyTree(ctx: Context, tree: Uri, src: File, parentId: String, rel: String, st: Stats) {
        val list = src.listFiles() ?: return
        val remote = children(ctx, tree, parentId)
        for (f in list.sortedBy { it.name }) {
            val relPath = if (rel.isEmpty()) f.name else "$rel/${f.name}"
            if (ProjectSavePlan.shouldSkip(relPath)) continue
            val ex = remote[f.name]
            if (f.isDirectory) {
                val id = if (ex != null && ex.isDir) ex.docId else {
                    val u = DocumentsContract.createDocument(ctx.contentResolver,
                        DocumentsContract.buildDocumentUriUsingTree(tree, parentId), DocumentsContract.Document.MIME_TYPE_DIR, f.name)
                        ?: throw java.io.IOException("mkdir ${f.name}")
                    DocumentsContract.getDocumentId(u)
                }
                copyTree(ctx, tree, f, id, relPath, st)
            } else if (ex == null || ex.isDir || ProjectSavePlan.needsCopy(f.length(), f.lastModified(), ex.size, ex.modified)) {
                if (ex != null && !ex.isDir) overwrite(ctx, tree, ex.docId, f, st) else putFile(ctx, tree, parentId, f.name, f, st)
            }
        }
    }

    private fun putFile(ctx: Context, tree: Uri, parentId: String, name: String, f: File, st: Stats) {
        val u = DocumentsContract.createDocument(ctx.contentResolver,
            DocumentsContract.buildDocumentUriUsingTree(tree, parentId), mimeFor(name), name)
            ?: throw java.io.IOException("create $name")
        write(ctx, u, f, st)
    }

    private fun overwrite(ctx: Context, tree: Uri, docId: String, f: File, st: Stats) =
        write(ctx, DocumentsContract.buildDocumentUriUsingTree(tree, docId), f, st)

    private fun write(ctx: Context, uri: Uri, f: File, st: Stats) {
        ctx.contentResolver.openOutputStream(uri, "wt")?.use { out -> f.inputStream().use { it.copyTo(out) } }
            ?: throw java.io.IOException("open ${f.name}")
        st.copied++; st.bytes += f.length()
    }
}
