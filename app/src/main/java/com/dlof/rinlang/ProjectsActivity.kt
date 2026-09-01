package com.dlof.rinlang

import android.app.AlertDialog
import android.content.Intent
import android.graphics.drawable.GradientDrawable
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.Switch
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView

/**
 * شاشة "أنشئ مشروع": تعرض كل مشاريع Rin الموجودة (مجلدات داخل filesDir/projects)
 * وتسمح بإنشاء مشروع جديد، إعادة تسميته، حذفه، أو فتحه (ينتقل لشاشة الملفات
 * الخاصة به عبر [FilesActivity]).
 */
class ProjectsActivity : AppCompatActivity() {

    private lateinit var rvProjects: RecyclerView
    private lateinit var txtEmpty: View
    private lateinit var adapter: ProjectsAdapter

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_projects)
        BottomNavHelper.setup(this, BottomNavTab.PROJECTS)

        findViewById<TextView>(R.id.txtToolbarTitle).text = getString(R.string.projects_screen_title)
        findViewById<View>(R.id.btnToolbarBack).setOnClickListener { finish() }

        rvProjects = findViewById(R.id.rvProjects)
        txtEmpty = findViewById(R.id.txtEmptyProjects)
        val fabNewProject: View = findViewById(R.id.fabNewProject)

        adapter = ProjectsAdapter(
            onOpen = { project -> openProject(project) },
            onRename = { project -> showRenameDialog(project) },
            onDelete = { project -> showDeleteConfirm(project) }
        )
        rvProjects.layoutManager = LinearLayoutManager(this)
        rvProjects.adapter = adapter

        fabNewProject.setOnClickListener { showCreateDialog() }
    }

    override fun onResume() {
        super.onResume()
        refresh()
    }

    private fun refresh() {
        val projects = ProjectManager.listProjects(this)
        adapter.submit(projects)
        txtEmpty.visibility = if (projects.isEmpty()) View.VISIBLE else View.GONE
    }

    private fun openProject(project: Project) {
        val intent = Intent(this, FilesActivity::class.java)
        intent.putExtra(FilesActivity.EXTRA_PROJECT_NAME, project.name)
        startActivity(intent)
    }

    /**
     * حوار "مشروع جديد": اسم المشروع + شبكة 2×2 من شرائح اختيار النوع (Container/Table/UI/
     * Free Project، انظر [ProjectType]). عند الضغط على "إنشاء" تظهر مراحل الإنشاء
     * (جاري التحميل.. / يتم التجهيز.. / تم..) عبر [ProjectCreationProgressDialog]، ثم يُفتح
     * المشروع تلقائياً في [FilesActivity].
     */
    private fun showCreateDialog() {
        val view = LayoutInflater.from(this).inflate(R.layout.dialog_create_project, null)
        val input: EditText = view.findViewById(R.id.inputProjectName)
        // بطاقات نوع المشروع أصبحت LinearLayout (أيقونة + عنوان + وصف) بدل TextView مفردة،
        // لكن منطق التحديد (isSelected) نفسه لأن bg_project_type_chip.xml selector يعمل على أي View.
        val chipContainer: View = view.findViewById(R.id.chipTypeContainer)
        val chipTable: View = view.findViewById(R.id.chipTypeTable)
        val chipUi: View = view.findViewById(R.id.chipTypeUi)
        val chipFree: View = view.findViewById(R.id.chipTypeFree)
        val chips = mapOf(
            chipContainer to ProjectType.CONTAINER,
            chipTable to ProjectType.TABLE,
            chipUi to ProjectType.UI,
            chipFree to ProjectType.FREE
        )

        // قسم "رسم الواجهة" (يظهر فقط عند اختيار نوع UI): توب بار/بلا توب بار، قائمة جانبية/بلا
        // قائمة جانبية، ولون أساسي — تُقرأ كلها عند الضغط على "إنشاء" وتُمرَّر كـ
        // ProjectManager.UiDesignOptions لتوليد main.rin المطابق (انظر ProjectManager.kt).
        val sectionUiDesign: View = view.findViewById(R.id.sectionUiDesign)
        val switchUiTopBar: Switch = view.findViewById(R.id.switchUiTopBar)
        val switchUiSidebar: Switch = view.findViewById(R.id.switchUiSidebar)
        val rowUiColors: LinearLayout = view.findViewById(R.id.rowUiColors)

        // لوحة الألوان الأساسية المتاحة لاختيار المستخدم؛ أول لون (البنفسجي) هو الافتراضي
        // نفسه المستخدم سابقاً في قالب UI الثابت، حتى لا يتغيّر الشكل الافتراضي لمن لا يلمس هذا الخيار.
        val colorPalette = listOf(
            R.color.ui_design_color_purple,
            R.color.ui_design_color_blue,
            R.color.ui_design_color_green,
            R.color.ui_design_color_amber,
            R.color.ui_design_color_pink,
            R.color.ui_design_color_cyan
        ).map { ContextCompat.getColor(this, it) }
        var selectedColorIndex = 0

        val swatchSizePx = (30 * resources.displayMetrics.density).toInt()
        val swatchStrokePx = (2.5f * resources.displayMetrics.density).toInt()

        fun renderColorSwatches() {
            rowUiColors.removeAllViews()
            colorPalette.forEachIndexed { index, color ->
                val swatch = View(this)
                val params = LinearLayout.LayoutParams(0, swatchSizePx, 1f).apply {
                    val marginPx = (4 * resources.displayMetrics.density).toInt()
                    setMargins(marginPx, 0, marginPx, 0)
                }
                swatch.layoutParams = params
                swatch.background = GradientDrawable().apply {
                    shape = GradientDrawable.OVAL
                    setColor(color)
                    if (index == selectedColorIndex) {
                        setStroke(swatchStrokePx, ContextCompat.getColor(this@ProjectsActivity, R.color.rin_on_toolbar))
                    }
                }
                swatch.setOnClickListener {
                    selectedColorIndex = index
                    renderColorSwatches()
                }
                rowUiColors.addView(swatch)
            }
        }
        renderColorSwatches()

        var selectedType = ProjectType.FREE
        fun selectChip(chip: View) {
            selectedType = chips.getValue(chip)
            chips.keys.forEach { it.isSelected = it === chip }
            sectionUiDesign.visibility = if (selectedType == ProjectType.UI) View.VISIBLE else View.GONE
        }
        chips.keys.forEach { chip -> chip.setOnClickListener { selectChip(chip) } }
        selectChip(chipFree)

        AlertDialog.Builder(this)
            .setTitle(R.string.new_project_title)
            .setView(view)
            .setPositiveButton(R.string.create) { _, _ ->
                val name = input.text.toString()
                val uiOptions = ProjectManager.UiDesignOptions(
                    topBar = switchUiTopBar.isChecked,
                    sidebar = switchUiSidebar.isChecked,
                    primaryColor = String.format("#%06X", 0xFFFFFF and colorPalette[selectedColorIndex])
                )
                ProjectCreationProgressDialog(this).run(
                    work = { ProjectManager.createProject(this, name, selectedType, uiOptions) },
                    onDone = { project, errorMessage ->
                        if (project != null) {
                            refresh()
                            openProject(project)
                        } else {
                            Toast.makeText(this, errorMessage ?: getString(R.string.project_name_hint), Toast.LENGTH_SHORT).show()
                        }
                    }
                )
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun showRenameDialog(project: Project) {
        val input = EditText(this)
        input.setText(project.name)
        AlertDialog.Builder(this)
            .setTitle(R.string.rename_project_title)
            .setView(input)
            .setPositiveButton(R.string.rename) { _, _ ->
                try {
                    ProjectManager.renameProject(project, input.text.toString())
                    refresh()
                } catch (e: IllegalArgumentException) {
                    Toast.makeText(this, e.message, Toast.LENGTH_SHORT).show()
                }
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun showDeleteConfirm(project: Project) {
        AlertDialog.Builder(this)
            .setTitle(R.string.delete_project_title)
            .setMessage(getString(R.string.delete_project_confirm, project.name))
            .setPositiveButton(R.string.delete) { _, _ ->
                ProjectManager.deleteProject(project)
                refresh()
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }
}

private class ProjectsAdapter(
    val onOpen: (Project) -> Unit,
    val onRename: (Project) -> Unit,
    val onDelete: (Project) -> Unit
) : RecyclerView.Adapter<ProjectsAdapter.VH>() {

    private var items: List<Project> = emptyList()

    fun submit(newItems: List<Project>) {
        items = newItems
        notifyDataSetChanged()
    }

    class VH(view: View) : RecyclerView.ViewHolder(view) {
        val txtName: TextView = view.findViewById(R.id.txtProjectName)
        val txtType: TextView = view.findViewById(R.id.txtProjectType)
        val txtMeta: TextView = view.findViewById(R.id.txtProjectMeta)
        val btnRename: View = view.findViewById(R.id.btnRenameProject)
        val btnDelete: View = view.findViewById(R.id.btnDeleteProject)
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): VH {
        val view = LayoutInflater.from(parent.context).inflate(R.layout.item_project, parent, false)
        return VH(view)
    }

    override fun onBindViewHolder(holder: VH, position: Int) {
        val project = items[position]
        val context = holder.itemView.context
        holder.txtName.text = project.name
        holder.txtType.text = typeLabel(context, project.type)
        val (iconRes, colorRes) = typeIconAndColor(project.type)
        val color = androidx.core.content.ContextCompat.getColor(context, colorRes)
        holder.txtType.setTextColor(color)
        // نضبط حجم الأيقونة يدوياً بدل setCompoundDrawablesWithIntrinsicBounds لأن حجمها
        // الأصلي (24dp) أكبر من ارتفاع شارة صغيرة كهذه.
        val iconSizePx = (11 * context.resources.displayMetrics.density).toInt()
        val icon = androidx.core.content.ContextCompat.getDrawable(context, iconRes)?.mutate()
        icon?.setTint(color)
        icon?.setBounds(0, 0, iconSizePx, iconSizePx)
        holder.txtType.setCompoundDrawables(icon, null, null, null)
        val fileCount = ProjectManager.listFiles(project).size
        holder.txtMeta.text = context.getString(R.string.project_meta_format, fileCount)
        holder.itemView.setOnClickListener { onOpen(project) }
        holder.btnRename.setOnClickListener { onRename(project) }
        holder.btnDelete.setOnClickListener { onDelete(project) }
    }

    override fun getItemCount(): Int = items.size

    /** نص شارة نوع المشروع المعروضة بجانب اسمه في القائمة. */
    private fun typeLabel(context: android.content.Context, type: ProjectType): String = when (type) {
        ProjectType.CONTAINER -> context.getString(R.string.project_type_container)
        ProjectType.TABLE -> context.getString(R.string.project_type_table)
        ProjectType.UI -> context.getString(R.string.project_type_ui)
        ProjectType.FREE -> context.getString(R.string.project_type_free)
    }

    /** أيقونة + لون هوية شارة نوع المشروع، بنفس الأيقونات المستخدمة في حوار "مشروع جديد". */
    private fun typeIconAndColor(type: ProjectType): Pair<Int, Int> = when (type) {
        ProjectType.CONTAINER -> R.drawable.ic_type_container to R.color.project_type_container_color
        ProjectType.TABLE -> R.drawable.ic_type_table to R.color.project_type_table_color
        ProjectType.UI -> R.drawable.ic_type_ui to R.color.project_type_ui_color
        ProjectType.FREE -> R.drawable.ic_type_free to R.color.project_type_free_color
    }
}
