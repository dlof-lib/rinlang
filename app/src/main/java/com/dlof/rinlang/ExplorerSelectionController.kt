package com.dlof.rinlang

import android.app.Activity
import android.view.View
import android.widget.TextView
import androidx.core.view.isVisible
import androidx.recyclerview.widget.ItemTouchHelper
import androidx.recyclerview.widget.RecyclerView

/**
 * يربط ميزة تحديد الملفات والإيماءات (FileSelection.kt) بدرج "مستكشف المشروع" داخل المحرر:
 *  • ضغطة مستمرة على صف → بدء التحديد؛ سحب ↑↓ بعدها → تحديد نطاق (مع تمرير تلقائي عند الحافة).
 *  • في وضع التحديد: سحب رأسي من أيقونة الصف مباشرة يحدّد نطاقاً، والنقر يبدّل التحديد.
 *  • سحب الصف أفقياً "بعيداً عن حافة الدرج" = إعادة تسمية (السحب نحو الحافة يبقى لإغلاق الدرج
 *    كما هو، فلا تعارض مع إيماءة DrawerLayout).
 *  • شريط الإجراءات: تحديد الكل، تكرار، نقل، نسخ المسار، حذف.
 *
 * الملف المفتوح حالياً في المحرر (وأي ملف بتعديلات غير محفوظة) "محمي": يُتخطّى عند الحذف/النقل/
 * إعادة التسمية مع إعلام المستخدم، كي لا يفقد المحرر مساره أو يُفقد عمله.
 */
class ExplorerSelectionController(
    private val activity: Activity,
    private val rv: RecyclerView,
    private val adapter: ProjectExplorerAdapter,
    private val selection: SelectionModel,
    private val project: () -> Project?,
    private val currentFileRelPath: () -> String?,
    /** يعيد بناء الشجرة بعد أي عملية تغيّر القرص. */
    private val refreshTree: () -> Unit
) {
    private val bar: View = activity.findViewById(R.id.layoutExplorerSelectionBar)
    private val txtCount: TextView = activity.findViewById(R.id.txtExplorerSelCount)

    init {
        rv.addOnItemTouchListener(
            DragSelectTouchListener(
                rv = rv,
                model = selection,
                keyAt = { adapter.keyAt(it) },
                onChanged = { onSelectionChanged() },
                handleViewIds = intArrayOf(R.id.imgExplorerFileIcon, R.id.imgExplorerFolderIcon)
            )
        )

        // اتجاه "بعيداً عن حافة الدرج": الدرج على حافة START؛ في LTR الحافة يسار فالسحب المسموح
        // لليمين، وفي RTL الحافة يمين فالسحب المسموح لليسار.
        val rtl = activity.resources.configuration.layoutDirection == View.LAYOUT_DIRECTION_RTL
        val rename = RowSwipeCallback.Action(
            R.drawable.ic_edit_pencil,
            androidx.core.content.ContextCompat.getColor(activity, R.color.rin_accent),
            activity.getString(R.string.swipe_rename_label)
        ) { pos ->
            val p = project()
            val e = adapter.entryAt(pos)
            if (p != null && e != null) {
                FileBatchOps.showRenameDialog(activity, p, e, protectedPaths()) { refreshTree() }
            }
        }
        val swipe = RowSwipeCallback(
            ctx = activity,
            model = selection,
            rightAction = if (rtl) null else rename,
            leftAction = if (rtl) rename else null,
            cornerDp = 8f
        )
        ItemTouchHelper(swipe).attachToRecyclerView(rv)

        activity.findViewById<View>(R.id.btnExplorerSelClose).setOnClickListener { clear() }
        activity.findViewById<View>(R.id.btnExplorerSelAll).setOnClickListener {
            val all = adapter.entries().map { it.relPath }
            // نقرة ثانية بعد تحديد الكل تُلغيه (تبديل سريع).
            if (all.isNotEmpty() && all.all { it in selection }) selection.clear()
            else all.forEach { selection.add(it) }
            onSelectionChanged()
        }
        activity.findViewById<View>(R.id.btnExplorerSelDuplicate).setOnClickListener {
            FileBatchOps.duplicate(activity, selected()) { clear(); refreshTree() }
        }
        activity.findViewById<View>(R.id.btnExplorerSelMove).setOnClickListener {
            val p = project() ?: return@setOnClickListener
            FileBatchOps.showMoveDialog(activity, p, selected(), protectedPaths()) { clear(); refreshTree() }
        }
        activity.findViewById<View>(R.id.btnExplorerSelCopyPath).setOnClickListener {
            FileBatchOps.copyPaths(activity, selected())
        }
        activity.findViewById<View>(R.id.btnExplorerSelDelete).setOnClickListener {
            FileBatchOps.confirmDelete(activity, selected(), protectedPaths()) { clear(); refreshTree() }
        }
    }

    private fun selected(): List<FsEntry> = adapter.entries().filter { it.relPath in selection }

    private fun protectedPaths(): Set<String> {
        val dirty = project()?.let { EditorDirtyState.dirtyPaths(it.name) } ?: emptySet()
        val open = currentFileRelPath()
        return if (open == null) dirty else dirty + open
    }

    /** يُستدعى من المحرر بعد كل إعادة بناء للشجرة: يزيل من التحديد ما لم يعد ظاهراً. */
    fun onTreeRebuilt() {
        selection.retainOnly(adapter.entries().map { it.relPath }.toSet())
        updateBar()
    }

    fun onSelectionChanged() {
        adapter.refreshSelection()
        updateBar()
    }

    private fun updateBar() {
        bar.isVisible = selection.active
        if (selection.active) txtCount.text = activity.getString(R.string.selection_count_format, selection.count)
    }

    fun clear() {
        if (!selection.active) return
        selection.clear()
        onSelectionChanged()
    }

    /** يستهلك زر الرجوع إن كان وضع التحديد فعّالاً (يُلغيه فقط). */
    fun handleBack(): Boolean {
        if (!selection.active) return false
        clear()
        return true
    }
}
