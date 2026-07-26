package com.dlof.rinlang

import android.app.AlertDialog
import android.content.Intent
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.EditText
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
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

    private fun showCreateDialog() {
        val input = EditText(this)
        input.hint = getString(R.string.project_name_hint)
        AlertDialog.Builder(this)
            .setTitle(R.string.new_project_title)
            .setView(input)
            .setPositiveButton(R.string.create) { _, _ ->
                val name = input.text.toString()
                try {
                    val project = ProjectManager.createProject(this, name)
                    refresh()
                    openProject(project)
                } catch (e: IllegalArgumentException) {
                    Toast.makeText(this, e.message, Toast.LENGTH_SHORT).show()
                }
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
        holder.txtName.text = project.name
        val fileCount = ProjectManager.listFiles(project).size
        holder.txtMeta.text = holder.itemView.context.getString(R.string.project_meta_format, fileCount)
        holder.itemView.setOnClickListener { onOpen(project) }
        holder.btnRename.setOnClickListener { onRename(project) }
        holder.btnDelete.setOnClickListener { onDelete(project) }
    }

    override fun getItemCount(): Int = items.size
}
