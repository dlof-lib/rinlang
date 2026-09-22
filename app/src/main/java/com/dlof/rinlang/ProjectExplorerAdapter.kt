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
    private val onFileClicked: (RinFile) -> Unit
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

    override fun onBindViewHolder(holder: RecyclerView.ViewHolder, position: Int) {
        val density = holder.itemView.resources.displayMetrics.density
        when (val node = nodes[position]) {
            is Node.FolderNode -> {
                holder as FolderVH
                applyIndent(holder.itemView, holder.basePaddingStart, node.depth, density)
                holder.name.text = node.folder.name
                // سهم يدور 90° عند التوسيع بدل أيقونتين منفصلتين (مفتوح/مغلق).
                holder.chevron.rotation = if (node.expanded) 90f else 0f
                holder.itemView.setOnClickListener { onFolderToggled(node.folder) }
            }
            is Node.FileNode -> {
                holder as FileVH
                applyIndent(holder.itemView, holder.basePaddingStart, node.depth, density)
                holder.name.text = node.file.name
                FileIconResolver.load(holder.icon, node.file.file)
                val isSelected = node.file.relPath == selectedRelPath
                holder.itemView.setBackgroundResource(
                    if (isSelected) R.drawable.bg_explorer_row_selected else R.drawable.bg_icon_btn
                )
                holder.name.setTypeface(holder.name.typeface, if (isSelected) android.graphics.Typeface.BOLD else android.graphics.Typeface.NORMAL)
                holder.dirtyDot.isVisible = dirtyPaths.contains(node.file.relPath)
                holder.itemView.setOnClickListener { onFileClicked(node.file) }
            }
        }
    }

    private fun applyIndent(view: View, basePaddingStart: Int, depth: Int, density: Float) {
        val indentPx = (INDENT_DP * depth * density).toInt()
        view.setPaddingRelative(basePaddingStart + indentPx, view.paddingTop, view.paddingEnd, view.paddingBottom)
    }

    class FolderVH(view: View) : RecyclerView.ViewHolder(view) {
        val chevron: ImageView = view.findViewById(R.id.imgExplorerChevron)
        val name: TextView = view.findViewById(R.id.txtExplorerFolderName)
        val basePaddingStart: Int = view.paddingStart
    }

    class FileVH(view: View) : RecyclerView.ViewHolder(view) {
        val icon: ImageView = view.findViewById(R.id.imgExplorerFileIcon)
        val name: TextView = view.findViewById(R.id.txtExplorerFileName)
        val dirtyDot: TextView = view.findViewById(R.id.txtExplorerDirtyDot)
        val basePaddingStart: Int = view.paddingStart
    }
}

/**
 * يبني قائمة [ProjectExplorerAdapter.Node] المسطّحة لشجرة ملفات/مجلدات مشروع عبر
 * [ProjectManager.listEntries]: يقرأ كل مستوى مجلد عند الحاجة فقط (المجلدات المطوية لا تُقرأ
 * محتوياتها إطلاقاً)، بدل تحميل كل ملفات المشروع دفعة واحدة مسبقاً.
 */
object ProjectExplorerTree {

    fun build(project: Project, expandedRelPaths: Set<String>): List<ProjectExplorerAdapter.Node> {
        val out = mutableListOf<ProjectExplorerAdapter.Node>()
        collect(project, "", 0, expandedRelPaths, out)
        return out
    }

    private fun collect(
        project: Project,
        relDir: String,
        depth: Int,
        expandedRelPaths: Set<String>,
        out: MutableList<ProjectExplorerAdapter.Node>
    ) {
        val (folders, files) = ProjectManager.listEntries(project, relDir)
        for (folder in folders.sortedBy { it.name.lowercase() }) {
            val expanded = expandedRelPaths.contains(folder.relPath)
            out += ProjectExplorerAdapter.Node.FolderNode(folder, depth, expanded)
            if (expanded) collect(project, folder.relPath, depth + 1, expandedRelPaths, out)
        }
        for (file in files.sortedBy { it.name.lowercase() }) {
            out += ProjectExplorerAdapter.Node.FileNode(file, depth)
        }
    }
}
