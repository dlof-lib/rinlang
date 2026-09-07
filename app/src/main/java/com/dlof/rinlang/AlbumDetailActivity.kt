package com.dlof.rinlang

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.recyclerview.widget.GridLayoutManager
import androidx.recyclerview.widget.RecyclerView

/** Album contents screen: projects are shown like pages inside an album. */
class AlbumDetailActivity : AppCompatActivity() {
    companion object { const val EXTRA_ALBUM_NAME = "extra_album_name" }
    private lateinit var album: String
    private lateinit var adapter: AlbumProjectsAdapter

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        album = intent.getStringExtra(EXTRA_ALBUM_NAME).orEmpty()
        if (album.isBlank()) { finish(); return }
        setContentView(R.layout.activity_album_detail)

        findViewById<TextView>(R.id.txtAlbumDetailTitle).text = album
        findViewById<View>(R.id.btnAlbumDetailBack).setOnClickListener { finish() }
        adapter = AlbumProjectsAdapter { project ->
            startActivity(android.content.Intent(this, FilesActivity::class.java).apply {
                putExtra(FilesActivity.EXTRA_PROJECT_NAME, project.name)
            })
        }
        findViewById<RecyclerView>(R.id.rvAlbumProjects).apply {
            layoutManager = GridLayoutManager(this@AlbumDetailActivity, 2)
            adapter = this@AlbumDetailActivity.adapter
        }
    }

    override fun onResume() {
        super.onResume()
        val projects = ProjectAlbumManager.projectsInAlbum(this, album)
        adapter.submit(projects)
        findViewById<TextView>(R.id.txtAlbumDetailCount).text = getString(R.string.album_projects_count_format, projects.size)
        findViewById<View>(R.id.albumDetailEmpty).visibility = if (projects.isEmpty()) View.VISIBLE else View.GONE
    }
}

private class AlbumProjectsAdapter(private val onOpen: (Project) -> Unit) : RecyclerView.Adapter<AlbumProjectsAdapter.VH>() {
    private var items: List<Project> = emptyList()
    fun submit(value: List<Project>) { items = value; notifyDataSetChanged() }
    class VH(v: View) : RecyclerView.ViewHolder(v) {
        val name: TextView = v.findViewById(R.id.txtAlbumProjectName)
        val type: TextView = v.findViewById(R.id.txtAlbumProjectType)
        val meta: TextView = v.findViewById(R.id.txtAlbumProjectMeta)
    }
    override fun onCreateViewHolder(p: ViewGroup, t: Int) = VH(LayoutInflater.from(p.context).inflate(R.layout/item_album_project, p, false))
    override fun onBindViewHolder(h: VH, p: Int) {
        val project = items[p]
        h.name.text = project.name
        h.type.text = project.type.id.uppercase()
        h.meta.text = h.itemView.context.getString(R.string.project_meta_format, ProjectManager.listFiles(project).size)
        h.itemView.setOnClickListener { onOpen(project) }
    }
    override fun getItemCount() = items.size
}
