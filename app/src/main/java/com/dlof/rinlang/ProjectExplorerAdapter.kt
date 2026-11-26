package com.dlof.rinlang

import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.ImageView
import android.widget.TextView
import androidx.core.view.isVisible
import androidx.recyclerview.widget.RecyclerView

/**
 * شجرة ملفات/مجلدات المشروع الحالي، معروضة داخل درج "مستكشف المشروع" المدمج في [MainActivity]
 * (view_project_explorer.xml) بدل الانتقال إلى شاشة [FilesActivity] المنفصلة كما كان يحدث سابقاً
 * (كل نقر على ملف هناك كان يفتح نسخة *جديدة* بالكامل من MainActivity عبر Intent).
 *
 * القائمة "مسطّحة" مبنية من [ProjectExplorerTree.build] حسب مجموعة المجلدات الموسَّعة حالياً
 * ([MainActivity.expandedExplorerFolders])، لا شجرة RecyclerView متداخلة حقيقية — أبسط وأكثر
 * ثباتاً لحجم المشاريع المعتادة هنا (عشرات إلى مئات الملفات كحد أقصى)، وكل نقر على مجلد يعيد
 * بناء القائمة كاملة (submit() -> notifyDataSetChanged) بدل diffing معقّد لا داعي له.
 */
class ProjectExplorerAdapter(
    private val onFolderToggled: (RinFolder) -> Unit,
    private val onFileClicked: (RinFile) -> Unit,
    /** حالة التحديد المتعدد المشتركة مع [ExplorerSelectionController]. */
    private val selection: SelectionModel = SelectionModel(),
    /** يُستدعى بعد تبديل تحديد صف بالنقر (أثناء وضع التحديد) لتحديث الشريط. */
    private val onSelectionChanged: () -> Unit = {}
) : RecyclerView.Adapter<RecyclerView.ViewHolder>() {

    sealed class Node {
        data class FolderNode(val folder: RinFolder, val depth: Int, val expanded: Boolean) : Node()
        data class FileNode(val file: RinFile, val depth: Int) : Node()
    }

    private var nodes: List<Node> = emptyList()
    private var selectedRelPath: String? = null
    private var dirtyPaths: Set<String> = emptySet()

    companion object {
        private const val TYPE_FOLDER = 0
        private const val TYPE_FILE = 1
        /** إزاحة بكسل مستقلة (dp) لكل مستوى تعشيش إضافي داخل الشجرة. */
        private const val INDENT_DP = 16
        /** حمولة تحديث جزئي: حالة التحديد فقط. */
        private const val PAYLOAD_SELECTION = "selection"
    }

    /** يستبدل محتوى القائمة بالكامل: عُقد جديدة، الملف المفتوح حالياً (لتظليل صفه)، والملفات
     *  ذات التعديلات غير المحفوظة (EditorDirtyState) لعرض نقطة ● الحقيقية أمام كل منها. */
    fun submit(nodes: List<Node>, selectedRelPath: String?, dirtyPaths: Set<String>) {
        this.nodes = nodes
        this.selectedRelPath = selectedRelPath
        this.dirtyPaths = dirtyPaths
        notifyDataSetChanged()
    }

    fun isEmpty(): Boolean = nodes.isEmpty()

    private var pinned: Set<String> = emptySet()

    /** المثبّتات (FilePins): نجمة قبل الاسم. تُستدعى قبل [submit]. */
    fun setPinned(paths: Set<String>) { pinned = paths }

    private fun applyPin(tv: TextView, key: String) {
        if (key in pinned) RsIcons.start(tv, R.drawable.rs_ic_star, 12, 4, androidx.core.content.ContextCompat.getColor(tv.context, R.color.rin_accent))
        else RsIcons.clear(tv)
    }

    /** مفتاح التحديد (المسار النسبي) للصف عند [position]. */
    fun keyAt(position: Int): String? = when (val n = nodes.getOrNull(position)) {
        is Node.FolderNode -> n.folder.relPath
        is Node.FileNode -> n.file.relPath
        null -> null
    }

    fun entryAt(position: Int): FsEntry? = when (val n = nodes.getOrNull(position)) {
        is Node.FolderNode -> FsEntry.FolderEntry(n.folder)
        is Node.FileNode -> FsEntry.FileEntry(n.file)
        null -> null
    }

    /** كل الصفوف الظاهرة حالياً (المجلدات المطوية لا تظهر أبناءها). */
    fun entries(): List<FsEntry> = nodes.map {
        when (it) {
            is Node.FolderNode -> FsEntry.FolderEntry(it.folder)
            is Node.FileNode -> FsEntry.FileEntry(it.file)
        }
    }

    fun refreshSelection() {
        if (nodes.isNotEmpty()) notifyItemRangeChanged(0, nodes.size, PAYLOAD_SELECTION)
    }

    private fun bindSelectionState(holder: RecyclerView.ViewHolder, position: Int) {
        val key = keyAt(position) ?: return
        val checked = key in selection
        // setBackgroundResource قد يعيد ضبط الحشوة؛ نحفظ إزاحة التعشيش (paddingStart) ونعيدها.
        val v = holder.itemView
        val ps = v.paddingStart; val pt = v.paddingTop; val pe = v.paddingEnd; val pb = v.paddingBottom
        try {
            applySelectionBackground(holder, key, checked)
        } finally {
            v.setPaddingRelative(ps, pt, pe, pb)
        }
    }

    private fun applySelectionBackground(holder: RecyclerView.ViewHolder, key: String, checked: Boolean) {
        when (holder) {
            is FolderVH -> {
                holder.check.isVisible = checked
                holder.itemView.setBackgroundResource(
                    if (checked) R.drawable.bg_explorer_row_checked else R.drawable.bg_icon_btn
                )
            }
            is FileVH -> {
                holder.check.isVisible = checked
                val opened = key == selectedRelPath
                holder.itemView.setBackgroundResource(
                    when {
                        checked -> R.drawable.bg_explorer_row_checked
                        opened -> R.drawable.bg_explorer_row_selected
                        else -> R.drawable.bg_icon_btn
                    }
                )
            }
        }
    }

    private fun toggleSelected(key: String) {
        selection.toggle(key)
        onSelectionChanged()
    }

    override fun getItemViewType(position: Int): Int =
        if (nodes[position] is Node.FolderNode) TYPE_FOLDER else TYPE_FILE

    override fun getItemCount(): Int = nodes.size

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): RecyclerView.ViewHolder {
        val inflater = LayoutInflater.from(parent.context)
        return if (viewType == TYPE_FOLDER) {
            FolderVH(inflater.inflate(R.layout.item_explorer_folder, parent, false))
        } else {
            FileVH(inflater.inflate(R.layout.item_explorer_file, parent, false))
        }
    }

    override fun onBindViewHolder(holder: RecyclerView.ViewHolder, position: Int, payloads: MutableList<Any>) {
        if (payloads.contains(PAYLOAD_SELECTION)) {
            bindSelectionState(holder, position)
        } else {
            super.onBindViewHolder(holder, position, payloads)
        }
    }

    override fun onBindViewHolder(holder: RecyclerView.ViewHolder, position: Int) {
        val density = holder.itemView.resources.displayMetrics.density
        when (val node = nodes[position]) {
            is Node.FolderNode -> {
                holder as FolderVH
                applyIndent(holder.itemView, holder.basePaddingStart, node.depth, density)
                holder.name.text = node.folder.name
                applyPin(holder.name, node.folder.relPath)
                // سهم يدور 90° عند التوسيع بدل أيقونتين منفصلتين (مفتوح/مغلق).
                holder.chevron.rotation = if (node.expanded) 90f else 0f
                holder.folderIcon.setImageResource(RsFolderIcons.forName(node.folder.name, open = node.expanded))
                val childCount = node.folder.dir.list()?.size ?: 0
                holder.count.text = if (childCount > 99) "99+" else childCount.toString()
                holder.count.visibility = if (childCount > 0) View.VISIBLE else View.GONE
                holder.itemView.setOnClickListener {
                    if (selection.active) toggleSelected(node.folder.relPath) else onFolderToggled(node.folder)
                }
                bindSelectionState(holder, position)
            }
            is Node.FileNode -> {
                holder as FileVH
                applyIndent(holder.itemView, holder.basePaddingStart, node.depth, density)
                holder.name.text = node.file.name
                applyPin(holder.name, node.file.relPath)
                FileIconResolver.load(holder.icon, node.file.file)
                val isSelected = node.file.relPath == selectedRelPath
                holder.itemView.setBackgroundResource(
                    if (isSelected) R.drawable.bg_explorer_row_selected else R.drawable.bg_icon_btn
                )
                holder.name.setTypeface(holder.name.typeface, if (isSelected) android.graphics.Typeface.BOLD else android.graphics.Typeface.NORMAL)
                holder.dirtyDot.isVisible = dirtyPaths.contains(node.file.relPath)
                holder.itemView.setOnClickListener {
                    if (selection.active) toggleSelected(node.file.relPath) else onFileClicked(node.file)
                }
                bindSelectionState(holder, position)
            }
        }
    }

    private fun applyIndent(view: View, basePaddingStart: Int, depth: Int, density: Float) {
        val indentPx = (INDENT_DP * depth * density).toInt()
        view.setPaddingRelative(basePaddingStart + indentPx, view.paddingTop, view.paddingEnd, view.paddingBottom)
    }

    class FolderVH(view: View) : RecyclerView.ViewHolder(view) {
        val chevron: ImageView = view.findViewById(R.id.imgExplorerChevron)
        val folderIcon: ImageView = view.findViewById(R.id.imgExplorerFolderIcon)
        val name: TextView = view.findViewById(R.id.txtExplorerFolderName)
        val count: TextView = view.findViewById(R.id.txtExplorerCount)
        val check: View = view.findViewById(R.id.imgExplorerCheck)
        val basePaddingStart: Int = view.paddingStart
    }

    class FileVH(view: View) : RecyclerView.ViewHolder(view) {
        val icon: ImageView = view.findViewById(R.id.imgExplorerFileIcon)
        val name: TextView = view.findViewById(R.id.txtExplorerFileName)
        val dirtyDot: TextView = view.findViewById(R.id.txtExplorerDirtyDot)
        val check: View = view.findViewById(R.id.imgExplorerCheck)
        val basePaddingStart: Int = view.paddingStart
    }
}

/**
 * يبني قائمة [ProjectExplorerAdapter.Node] المسطّحة لشجرة ملفات/مجلدات مشروع عبر
 * [ProjectManager.listEntries]: يقرأ كل مستوى مجلد عند الحاجة فقط (المجلدات المطوية لا تُقرأ
 * محتوياتها إطلاقاً)، بدل تحميل كل ملفات المشروع دفعة واحدة مسبقاً.
 */
object ProjectExplorerTree {

    fun build(project: Project, expandedRelPaths: Set<String>, ctx: android.content.Context? = null): List<ProjectExplorerAdapter.Node> {
        val out = mutableListOf<ProjectExplorerAdapter.Node>()
        collect(project, "", 0, expandedRelPaths, out, ctx)
        return out
    }

    private fun collect(
        project: Project,
        relDir: String,
        depth: Int,
        expandedRelPaths: Set<String>,
        out: MutableList<ProjectExplorerAdapter.Node>,
        ctx: android.content.Context?
    ) {
        val (folders, files) = ProjectManager.listEntries(project, relDir)
        val pins = if (ctx != null) FilePins.get(ctx, project.name) else emptySet()
        val sortedFolders = if (ctx != null) FileSort.folders(ctx, folders, pins) else folders.sortedBy { it.name.lowercase() }
        val sortedFiles = if (ctx != null) FileSort.files(ctx, files, pins) else files.sortedBy { it.name.lowercase() }
        for (folder in sortedFolders) {
            val expanded = expandedRelPaths.contains(folder.relPath)
            out += ProjectExplorerAdapter.Node.FolderNode(folder, depth, expanded)
            if (expanded) collect(project, folder.relPath, depth + 1, expandedRelPaths, out, ctx)
        }
        for (file in sortedFiles) {
            out += ProjectExplorerAdapter.Node.FileNode(file, depth)
        }
    }
}
