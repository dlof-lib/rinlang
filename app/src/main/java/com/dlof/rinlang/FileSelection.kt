package com.dlof.rinlang

import android.app.AlertDialog
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Rect
import android.graphics.RectF
import android.os.Handler
import android.os.Looper
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View
import android.view.ViewConfiguration
import android.widget.EditText
import android.widget.Toast
import androidx.annotation.DrawableRes
import androidx.core.content.ContextCompat
import androidx.recyclerview.widget.ItemTouchHelper
import androidx.recyclerview.widget.RecyclerView
import java.io.File
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min

/*
 * ميزة "تحديد الملفات" + إيماءات القوائم في RinStudio.
 *
 * تُستخدَم في موضعين بنفس الكود:
 *   1) شاشة الملفات [FilesActivity] (بطاقات).
 *   2) درج "مستكشف المشروع" داخل المحرر [MainActivity] عبر [ExplorerSelectionController].
 *
 * الإيماءات المدعومة على صفوف الملفات/المجلدات:
 *   • ضغطة مستمرة            → بدء وضع التحديد وتحديد الصف.
 *   • ضغطة مستمرة ثم سحب ↑ ↓ → تحديد نطاق من الصفوف (وإن بدأت من صف محدد فالسحب "يُلغي" التحديد).
 *     عند الاقتراب من حافة القائمة تتمرّر تلقائياً.
 *   • في وضع التحديد: سحب رأسي بدءاً من أيقونة الصف مباشرة (بلا ضغط مستمر) يحدّد نطاقاً.
 *   • سحب الصف أفقياً → إجراء سريع (يمين: إعادة تسمية، يسار: حذف بتأكيد).
 *   • نقرة على صف في وضع التحديد → تبديل تحديده.
 */

// ---------------------------------------------------------------------------------------------
// نموذج التحديد
// ---------------------------------------------------------------------------------------------

/** عنصر قابل للتحديد: ملف أو مجلد. المفتاح الفريد هو المسار النسبي من جذر المشروع. */
sealed class FsEntry {
    abstract val relPath: String
    abstract val name: String

    data class FileEntry(val file: RinFile) : FsEntry() {
        override val relPath: String get() = file.relPath
        override val name: String get() = file.name
    }

    data class FolderEntry(val folder: RinFolder) : FsEntry() {
        override val relPath: String get() = folder.relPath
        override val name: String get() = folder.name
    }

    val isFolder: Boolean get() = this is FolderEntry
}

/** مجموعة مفاتيح (relPath) محدَّدة حالياً. وضع التحديد "فعّال" ما دامت المجموعة غير فارغة. */
class SelectionModel {
    private val keys = LinkedHashSet<String>()

    val active: Boolean get() = keys.isNotEmpty()
    val count: Int get() = keys.size

    operator fun contains(key: String): Boolean = key in keys

    fun add(key: String): Boolean = keys.add(key)
    fun remove(key: String): Boolean = keys.remove(key)

    fun toggle(key: String) {
        if (!keys.add(key)) keys.remove(key)
    }

    fun clear() = keys.clear()

    fun snapshot(): Set<String> = LinkedHashSet(keys)

    fun restore(snapshot: Set<String>) {
        keys.clear()
        keys.addAll(snapshot)
    }

    /** يُبقي فقط المفاتيح الموجودة في [valid] (مثلاً بعد تغيير المجلد أو حذف عنصر). */
    fun retainOnly(valid: Set<String>): Boolean = keys.retainAll(valid)
}

// ---------------------------------------------------------------------------------------------
// السحب للتحديد (ضغطة مستمرة + سحب رأسي)
// ---------------------------------------------------------------------------------------------

/**
 * يلتقط الضغطة المستمرة على صف داخل [rv] فيبدأ وضع التحديد، ثم يحوّل السحب الرأسي التالي إلى
 * تحديد نطاق (من الصف الأول إلى الصف تحت الإصبع). يعمل أيضاً من "مقبض" (أيقونة الصف) مباشرة
 * عندما يكون وضع التحديد فعّالاً أصلاً.
 */
class DragSelectTouchListener(
    private val rv: RecyclerView,
    private val model: SelectionModel,
    /** مفتاح الصف عند الموضع، أو null إن كان غير قابل للتحديد. */
    private val keyAt: (Int) -> String?,
    /** يُستدعى بعد كل تغيير في [model] لتحديث الواجهة. */
    private val onChanged: () -> Unit,
    /** معرّفات Views داخل الصف تُعتبر "مقبض سحب" في وضع التحديد. */
    private val handleViewIds: IntArray = IntArray(0)
) : RecyclerView.OnItemTouchListener {

    private val ctx: Context = rv.context
    private val handler = Handler(Looper.getMainLooper())
    private val touchSlop = ViewConfiguration.get(ctx).scaledTouchSlop
    private val density = ctx.resources.displayMetrics.density
    private val edgePx = 56f * density
    private val maxScrollPx = (18f * density).toInt()
    private val handleInflatePx = (12f * density).toInt()

    private var downX = 0f
    private var downY = 0f
    private var downPos = RecyclerView.NO_POSITION
    private var handleCandidate = false

    private var dragging = false
    private var anchorPos = RecyclerView.NO_POSITION
    private var lastPos = RecyclerView.NO_POSITION
    private var baseline: Set<String> = emptySet()
    private var adding = true
    private var lastX = 0f
    private var lastY = 0f
    private var scrolling = false

    private val longPressRunnable = Runnable {
        if (downPos != RecyclerView.NO_POSITION) beginDrag(downPos)
    }

    private val autoScroll = object : Runnable {
        override fun run() {
            if (!dragging) { scrolling = false; return }
            val dy = when {
                lastY < edgePx -> -(maxScrollPx * ((edgePx - lastY) / edgePx)).toInt().coerceAtLeast(2)
                lastY > rv.height - edgePx -> (maxScrollPx * ((lastY - (rv.height - edgePx)) / edgePx)).toInt().coerceAtLeast(2)
                else -> 0
            }
            if (dy != 0) {
                rv.scrollBy(0, dy)
                updateRange()
                rv.postOnAnimation(this)
            } else {
                scrolling = false
            }
        }
    }

    override fun onInterceptTouchEvent(rv: RecyclerView, e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                reset()
                downX = e.x; downY = e.y
                val child = rv.findChildViewUnder(e.x, e.y)
                downPos = child?.let { rv.getChildAdapterPosition(it) } ?: RecyclerView.NO_POSITION
                if (downPos != RecyclerView.NO_POSITION && keyAt(downPos) == null) downPos = RecyclerView.NO_POSITION
                if (downPos != RecyclerView.NO_POSITION) {
                    handleCandidate = model.active && child != null && hitsHandle(child, e.x, e.y)
                    handler.postDelayed(longPressRunnable, ViewConfiguration.getLongPressTimeout().toLong())
                }
            }
            MotionEvent.ACTION_MOVE -> {
                if (dragging) return true
                val dx = e.x - downX
                val dy = e.y - downY
                if (abs(dx) > touchSlop || abs(dy) > touchSlop) {
                    handler.removeCallbacks(longPressRunnable)
                    if (handleCandidate && abs(dy) > abs(dx) && downPos != RecyclerView.NO_POSITION) {
                        beginDrag(downPos)
                        return true
                    }
                    handleCandidate = false
                }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                val was = dragging
                finish()
                return was
            }
        }
        return dragging
    }

    override fun onTouchEvent(rv: RecyclerView, e: MotionEvent) {
        when (e.actionMasked) {
            MotionEvent.ACTION_MOVE -> {
                lastX = e.x; lastY = e.y
                updateRange()
                if (!scrolling && (lastY < edgePx || lastY > rv.height - edgePx)) {
                    scrolling = true
                    rv.postOnAnimation(autoScroll)
                }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> finish()
        }
    }

    override fun onRequestDisallowInterceptTouchEvent(disallowIntercept: Boolean) {}

    private fun hitsHandle(child: View, x: Float, y: Float): Boolean {
        for (id in handleViewIds) {
            val h = child.findViewById<View>(id) ?: continue
            if (h.visibility != View.VISIBLE) continue
            val r = Rect()
            h.getDrawingRect(r)
            rv.offsetDescendantRectToMyCoords(h, r)
            r.inset(-handleInflatePx, -handleInflatePx)
            if (r.contains(x.toInt(), y.toInt())) return true
        }
        return false
    }

    private fun beginDrag(position: Int) {
        val key = keyAt(position) ?: return
        dragging = true
        anchorPos = position
        lastPos = position
        lastX = downX
        lastY = downY
        baseline = model.snapshot()
        // بدء من صف محدّد أصلاً داخل وضع تحديد فعّال = سحب لإلغاء التحديد.
        adding = !(model.active && key in model)
        rv.parent?.requestDisallowInterceptTouchEvent(true)
        if (AppSettings.isHapticFeedback(ctx)) rv.performHapticFeedback(HapticFeedbackConstants.LONG_PRESS)
        applyRange(position, position)
    }

    private fun updateRange() {
        if (!dragging) return
        val child = rv.findChildViewUnder(lastX.coerceIn(0f, (rv.width - 1).toFloat()), lastY.coerceIn(0f, (rv.height - 1).toFloat()))
        val pos = child?.let { rv.getChildAdapterPosition(it) } ?: RecyclerView.NO_POSITION
        if (pos == RecyclerView.NO_POSITION || pos == lastPos) return
        lastPos = pos
        applyRange(anchorPos, pos)
    }

    /** يعيد التحديد = (اللقطة الأصلية) ± كل الصفوف بين [a] و[b]، فيتقلّص النطاق طبيعياً عند التراجع. */
    private fun applyRange(a: Int, b: Int) {
        model.restore(baseline)
        for (i in min(a, b)..max(a, b)) {
            val k = keyAt(i) ?: continue
            if (adding) model.add(k) else model.remove(k)
        }
        if (AppSettings.isHapticFeedback(ctx) && b != anchorPos) rv.performHapticFeedback(HapticFeedbackConstants.CLOCK_TICK)
        onChanged()
    }

    private fun finish() {
        val was = dragging
        reset()
        if (was) rv.parent?.requestDisallowInterceptTouchEvent(false)
    }

    private fun reset() {
        handler.removeCallbacks(longPressRunnable)
        rv.removeCallbacks(autoScroll)
        dragging = false
        scrolling = false
        handleCandidate = false
        downPos = RecyclerView.NO_POSITION
        anchorPos = RecyclerView.NO_POSITION
        lastPos = RecyclerView.NO_POSITION
    }
}

// ---------------------------------------------------------------------------------------------
// السحب الأفقي على الصف (إجراء سريع)
// ---------------------------------------------------------------------------------------------

/**
 * سحب صف إلى اليمين/اليسار (اتجاه فيزيائي بغضّ النظر عن RTL) يُظهر إجراءً ملوّناً خلفه:
 * [rightAction] عند السحب لليمين، [leftAction] عند السحب لليسار. أحدهما قد يكون null لتعطيله.
 * بعد التنفيذ يُعاد الصف مكانه فوراً (الإجراء نفسه هو من يغيّر القائمة بعد التأكيد).
 */
class RowSwipeCallback(
    private val ctx: Context,
    private val model: SelectionModel,
    private val rightAction: Action?,
    private val leftAction: Action?,
    private val cornerDp: Float = 14f,
    private val canSwipe: (RecyclerView.ViewHolder) -> Boolean = { true }
) : ItemTouchHelper.SimpleCallback(0, ItemTouchHelper.LEFT or ItemTouchHelper.RIGHT) {

    class Action(
        @DrawableRes val icon: Int,
        val color: Int,
        val label: String,
        val onTriggered: (position: Int) -> Unit
    )

    private val density = ctx.resources.displayMetrics.density
    private val bgPaint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFFFFFFFF.toInt()
        textSize = 12f * density
        isFakeBoldText = true
    }
    private val rect = RectF()
    private var crossed = false

    override fun getSwipeDirs(recyclerView: RecyclerView, viewHolder: RecyclerView.ViewHolder): Int {
        if (model.active || !canSwipe(viewHolder)) return 0
        var dirs = 0
        if (rightAction != null) dirs = dirs or ItemTouchHelper.RIGHT
        if (leftAction != null) dirs = dirs or ItemTouchHelper.LEFT
        return dirs
    }

    override fun onMove(rv: RecyclerView, vh: RecyclerView.ViewHolder, target: RecyclerView.ViewHolder) = false

    override fun getSwipeThreshold(viewHolder: RecyclerView.ViewHolder) = 0.4f

    override fun onSwiped(viewHolder: RecyclerView.ViewHolder, direction: Int) {
        val pos = viewHolder.bindingAdapterPosition
        if (pos == RecyclerView.NO_POSITION) return
        // أعِد الصف إلى مكانه أولاً؛ الإجراء (حوار تأكيد/إعادة تسمية) يتولّى تحديث القائمة بعدها.
        viewHolder.bindingAdapter?.notifyItemChanged(pos)
        val action = if (direction == ItemTouchHelper.RIGHT) rightAction else leftAction
        action?.onTriggered?.invoke(pos)
    }

    override fun clearView(recyclerView: RecyclerView, viewHolder: RecyclerView.ViewHolder) {
        crossed = false
        super.clearView(recyclerView, viewHolder)
    }

    override fun onChildDraw(
        c: Canvas, recyclerView: RecyclerView, viewHolder: RecyclerView.ViewHolder,
        dX: Float, dY: Float, actionState: Int, isCurrentlyActive: Boolean
    ) {
        if (actionState == ItemTouchHelper.ACTION_STATE_SWIPE && dX != 0f) {
            val v = viewHolder.itemView
            val action = if (dX > 0) rightAction else leftAction
            if (action != null) {
                val progress = (abs(dX) / v.width).coerceIn(0f, 1f)
                bgPaint.color = action.color
                bgPaint.alpha = (110 + 145 * min(1f, progress / 0.4f)).toInt().coerceIn(0, 255)
                val r = cornerDp * density
                if (dX > 0) rect.set(v.left.toFloat(), v.top.toFloat(), v.left + dX, v.bottom.toFloat())
                else rect.set(v.right + dX, v.top.toFloat(), v.right.toFloat(), v.bottom.toFloat())
                c.drawRoundRect(rect, r, r, bgPaint)

                val icon = ContextCompat.getDrawable(ctx, action.icon)?.mutate()
                val size = (22 * density).toInt()
                val pad = (18 * density).toInt()
                val cy = (v.top + v.bottom) / 2
                if (icon != null) {
                    icon.setTint(0xFFFFFFFF.toInt())
                    val left = if (dX > 0) v.left + pad else v.right - pad - size
                    // لا نرسم الأيقونة قبل أن يتّسع المكان لها.
                    if (abs(dX) > pad + size / 2) {
                        icon.setBounds(left, cy - size / 2, left + size, cy + size / 2)
                        icon.draw(c)
                        val labelW = textPaint.measureText(action.label)
                        if (abs(dX) > pad + size + 8 * density + labelW + pad / 2) {
                            val tx = if (dX > 0) left + size + 8 * density else left - 8 * density - labelW
                            c.drawText(action.label, tx, cy + textPaint.textSize / 3f, textPaint)
                        }
                    }
                }
                if (isCurrentlyActive) {
                    val past = progress >= 0.4f
                    if (past && !crossed && AppSettings.isHapticFeedback(ctx)) {
                        recyclerView.performHapticFeedback(HapticFeedbackConstants.CLOCK_TICK)
                    }
                    crossed = past
                }
            }
        }
        super.onChildDraw(c, recyclerView, viewHolder, dX, dY, actionState, isCurrentlyActive)
    }
}

// ---------------------------------------------------------------------------------------------
// عمليات الدُّفعات على العناصر المحدَّدة
// ---------------------------------------------------------------------------------------------

object FileBatchOps {

    private class Tally(var ok: Int = 0, var skipped: Int = 0, var failed: Int = 0)

    /** يحذف من القائمة أي عنصر يقع داخل مجلد محدَّد آخر في نفس القائمة (يُعالَج مع أبيه). */
    fun normalize(entries: List<FsEntry>): List<FsEntry> {
        val folders = entries.filterIsInstance<FsEntry.FolderEntry>().map { it.relPath }
        return entries.filter { e -> folders.none { f -> e.relPath.startsWith("$f/") } }
    }

    /** عنصر "محمي": مفتوح الآن في المحرر أو فيه تعديلات غير محفوظة (أو مجلد يحويه). */
    private fun isProtected(entry: FsEntry, protectedPaths: Set<String>): Boolean =
        protectedPaths.any { p ->
            p == entry.relPath || (entry.isFolder && p.startsWith(entry.relPath + "/"))
        }

    private fun report(ctx: Context, t: Tally) {
        val parts = mutableListOf<String>()
        if (t.ok > 0) parts += ctx.getString(R.string.selection_done_toast, t.ok)
        if (t.skipped > 0) parts += ctx.getString(R.string.selection_skipped_toast, t.skipped)
        if (t.failed > 0) parts += ctx.getString(R.string.selection_failed_toast, t.failed)
        if (parts.isNotEmpty()) Toast.makeText(ctx, parts.joinToString("\n"), Toast.LENGTH_LONG).show()
    }

    // ---- حذف ----

    fun confirmDelete(
        ctx: Context, entries: List<FsEntry>, protectedPaths: Set<String>, projectName: String? = null,
        onDone: () -> Unit
    ) {
        val list = normalize(entries)
        if (list.isEmpty()) return
        AlertDialog.Builder(ctx)
            .setTitle(R.string.selection_delete_title)
            .setMessage(ctx.getString(R.string.selection_delete_confirm, list.size))
            .setPositiveButton(R.string.delete) { _, _ ->
                val t = Tally()
                val toTrash = mutableListOf<File>()
                for (e in list) {
                    if (isProtected(e, protectedPaths)) { t.skipped++; continue }
                    toTrash += when (e) {
                        is FsEntry.FileEntry -> e.file.file
                        is FsEntry.FolderEntry -> e.folder.dir
                    }
                }
                // الحذف إلى سلة مؤقتة يتيح "تراجع" فورياً بدل حذف نهائي.
                val batch = if (toTrash.isEmpty()) null else FileTrash.trash(ctx, projectName ?: "project", toTrash)
                t.ok = batch?.moves?.size ?: 0
                t.failed = toTrash.size - t.ok
                report(ctx, t)
                onDone()
                val host = (ctx as? android.app.Activity)?.findViewById<View>(android.R.id.content)
                if (batch != null && host != null) {
                    com.google.android.material.snackbar.Snackbar
                        .make(host, ctx.getString(R.string.selection_done_toast, t.ok), com.google.android.material.snackbar.Snackbar.LENGTH_LONG)
                        .setAction(R.string.undo_action) {
                            FileTrash.restore(batch)
                            onDone()
                        }
                        .show()
                }
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    // ---- تكرار ----

    fun duplicate(ctx: Context, entries: List<FsEntry>, onDone: () -> Unit) {
        val t = Tally()
        for (e in normalize(entries)) {
            try {
                when (e) {
                    is FsEntry.FileEntry -> ProjectManager.duplicateFile(e.file)
                    is FsEntry.FolderEntry -> duplicateFolder(e.folder)
                }
                t.ok++
            } catch (_: Throwable) {
                t.failed++
            }
        }
        report(ctx, t)
        onDone()
    }

    private fun duplicateFolder(folder: RinFolder) {
        val parent = folder.dir.parentFile ?: throw IllegalStateException("no parent")
        var n = 2
        var target: File
        do {
            target = File(parent, "${folder.name} ($n)")
            n++
        } while (target.exists())
        require(folder.dir.copyRecursively(target, overwrite = false)) { "copy failed" }
    }

    // ---- نقل ----

    fun showMoveDialog(
        ctx: Context, project: Project, entries: List<FsEntry>, protectedPaths: Set<String>,
        onDone: () -> Unit
    ) {
        val list = normalize(entries)
        if (list.isEmpty()) return
        val selectedFolders = list.filterIsInstance<FsEntry.FolderEntry>().map { it.relPath }

        val allDirs = mutableListOf("")
        fun walk(relDir: String) {
            val (folders, _) = ProjectManager.listEntries(project, relDir)
            for (f in folders) {
                allDirs += f.relPath
                walk(f.relPath)
            }
        }
        walk("")
        // لا يجوز نقل مجلد إلى نفسه أو إلى أحد أبنائه.
        val candidates = allDirs.filter { d ->
            selectedFolders.none { f -> d == f || d.startsWith("$f/") }
        }
        if (candidates.isEmpty()) {
            Toast.makeText(ctx, R.string.move_no_folders, Toast.LENGTH_SHORT).show()
            return
        }
        val labels = candidates.map { if (it.isEmpty()) ctx.getString(R.string.move_root_option) else it }.toTypedArray()
        AlertDialog.Builder(ctx)
            .setTitle(ctx.getString(R.string.selection_move_title, list.size))
            .setItems(labels) { _, which ->
                val destRel = candidates[which]
                val destDir = ProjectManager.resolveDir(project, destRel)
                destDir.mkdirs()
                val t = Tally()
                val moved = mutableListOf<Pair<File, File>>() // (الأصل, الوجهة) للتراجع
                for (e in list) {
                    if (isProtected(e, protectedPaths)) { t.skipped++; continue }
                    val src: File = when (e) {
                        is FsEntry.FileEntry -> e.file.file
                        is FsEntry.FolderEntry -> e.folder.dir
                    }
                    val target = File(destDir, src.name)
                    when {
                        target.canonicalPath == src.canonicalPath -> t.skipped++
                        target.exists() -> t.skipped++
                        src.renameTo(target) -> { t.ok++; moved += src to target }
                        else -> t.failed++
                    }
                }
                report(ctx, t)
                onDone()
                val host = (ctx as? android.app.Activity)?.findViewById<View>(android.R.id.content)
                if (moved.isNotEmpty() && host != null) {
                    com.google.android.material.snackbar.Snackbar
                        .make(host, ctx.getString(R.string.selection_done_toast, moved.size),
                            com.google.android.material.snackbar.Snackbar.LENGTH_LONG)
                        .setAction(R.string.undo_action) {
                            moved.forEach { (orig, now) -> if (!orig.exists() && now.exists()) now.renameTo(orig) }
                            onDone()
                        }
                        .show()
                }
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    // ---- نسخ المسارات ----

    fun copyPaths(ctx: Context, entries: List<FsEntry>) {
        val list = normalize(entries)
        if (list.isEmpty()) return
        val cm = ctx.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
        cm.setPrimaryClip(ClipData.newPlainText("paths", list.joinToString("\n") { it.relPath }))
        Toast.makeText(ctx, ctx.getString(R.string.selection_paths_copied_toast, list.size), Toast.LENGTH_SHORT).show()
    }

    // ---- إعادة تسمية (للإيماءة السريعة داخل درج المستكشف) ----

    fun showRenameDialog(
        ctx: Context, project: Project, entry: FsEntry, protectedPaths: Set<String>, onDone: () -> Unit
    ) {
        if (isProtected(entry, protectedPaths)) {
            Toast.makeText(ctx, ctx.getString(R.string.selection_skipped_toast, 1), Toast.LENGTH_SHORT).show()
            return
        }
        val input = EditText(ctx)
        input.setText(entry.name)
        val dot = entry.name.lastIndexOf('.')
        input.setSelection(0, if (!entry.isFolder && dot > 0) dot else entry.name.length)
        AlertDialog.Builder(ctx)
            .setTitle(if (entry.isFolder) R.string.rename_folder_title else R.string.rename_file_title)
            .setView(input)
            .setPositiveButton(R.string.rename) { _, _ ->
                val newName = input.text.toString().trim()
                if (newName.isEmpty() || newName == entry.name) return@setPositiveButton
                try {
                    when (entry) {
                        is FsEntry.FileEntry -> ProjectManager.renameFile(project, entry.file, newName)
                        is FsEntry.FolderEntry -> ProjectManager.renameFolder(entry.folder, newName)
                    }
                    onDone()
                } catch (t: Throwable) {
                    Toast.makeText(ctx, "${t.message}", Toast.LENGTH_LONG).show()
                }
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }
}
