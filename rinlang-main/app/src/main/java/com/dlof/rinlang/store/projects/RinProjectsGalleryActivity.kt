package com.dlof.rinlang.store.projects

import android.app.AlertDialog
import android.content.Intent
import android.graphics.BitmapFactory
import android.os.Bundle
import android.text.Editable
import android.text.TextWatcher
import android.util.Base64
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.EditText
import android.widget.ImageButton
import android.widget.ImageView
import android.widget.TextView
import android.widget.Toast
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.dlof.rinlang.FilesActivity
import com.dlof.rinlang.ProjectManager
import com.dlof.rinlang.R
import com.dlof.rinlang.network.BaseConnectivityActivity
import com.dlof.rinlang.widgets.ShimmerLayout
import com.google.android.material.chip.Chip
import com.google.android.material.chip.ChipGroup

/**
 * الصفحة الرئيسية لـ "معرض مشاريع Rin": تصفّح مشاريع كاملة نشرها مستخدمو Rin الآخرون (قراءة
 * عامة، لا تحتاج تسجيل دخول)، بحث + تصنيفات + فرز، وتنزيل أي مشروع كنسخة محلية جديدة بضغطة
 * واحدة. بطاقة كل مشروع تعرض صورة مصغّرة مُولَّدة من كوده الحقيقي (راجع [CodeThumbnailGenerator])
 * بدل أي صورة عامة.
 */
class RinProjectsGalleryActivity : BaseConnectivityActivity() {

    companion object {
        private const val CATEGORY_ALL = "__all__"
    }

    private enum class SortMode(val labelRes: Int) {
        NEWEST(R.string.store_sort_newest),
        DOWNLOADS(R.string.store_sort_downloads),
        NAME(R.string.store_sort_name)
    }

    private lateinit var rvProjects: RecyclerView
    private lateinit var shimmerSkeleton: ShimmerLayout
    private lateinit var txtEmpty: View
    private lateinit var adapter: GalleryAdapter
    private lateinit var edtSearch: EditText
    private lateinit var chipGroupCategory: ChipGroup

    private var allProjects: List<RinProject> = emptyList()
    private var selectedCategory: String = CATEGORY_ALL
    private var sortMode: SortMode = SortMode.NEWEST

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_rin_projects_gallery)

        findViewById<TextView>(R.id.txtToolbarTitle).text = getString(R.string.project_gallery_title)
        findViewById<View>(R.id.btnToolbarBack).setOnClickListener { finish() }

        rvProjects = findViewById(R.id.rvGalleryProjects)
        shimmerSkeleton = findViewById(R.id.shimmerGallerySkeleton)
        txtEmpty = findViewById(R.id.txtGalleryEmpty)
        edtSearch = findViewById(R.id.edtGallerySearch)
        chipGroupCategory = findViewById(R.id.chipGroupGalleryCategory)

        adapter = GalleryAdapter(
            onOpenDetail = { project -> showDetailDialog(project) },
            onDownload = { project -> confirmAndDownload(project) }
        )
        rvProjects.layoutManager = LinearLayoutManager(this)
        rvProjects.adapter = adapter

        edtSearch.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
            override fun afterTextChanged(s: Editable?) = applyFilters()
        })

        findViewById<View>(R.id.btnGalleryOpenPublish).setOnClickListener {
            PublishRinProjectActivity.start(this)
        }

        runIfOnline { loadProjects() }
    }

    override fun onConnectionRestored() {
        loadProjects()
    }

    override fun onResume() {
        super.onResume()
        if (::adapter.isInitialized) adapter.notifyDataSetChanged()
    }

    private fun loadProjects() {
        showSkeleton()
        RinProjectRepository.fetchAll { projects ->
            hideSkeleton()
            allProjects = projects
            rebuildCategoryChips()
            applyFilters()
        }
    }

    private fun showSkeleton() {
        shimmerSkeleton.visibility = View.VISIBLE
        shimmerSkeleton.startShimmer()
        rvProjects.visibility = View.GONE
        txtEmpty.visibility = View.GONE
    }

    private fun hideSkeleton() {
        shimmerSkeleton.stopShimmer()
        shimmerSkeleton.visibility = View.GONE
        rvProjects.visibility = View.VISIBLE
    }

    private fun rebuildCategoryChips() {
        chipGroupCategory.removeAllViews()
        val categories = listOf(CATEGORY_ALL) + allProjects.map { it.category }.distinct().sorted()
        categories.forEach { category ->
            val chip = Chip(this).apply {
                text = if (category == CATEGORY_ALL) getString(R.string.store_category_all) else category
                isCheckable = true
                isChecked = category == selectedCategory
                setOnClickListener {
                    selectedCategory = category
                    for (i in 0 until chipGroupCategory.childCount) {
                        (chipGroupCategory.getChildAt(i) as? Chip)?.isChecked = false
                    }
                    isChecked = true
                    applyFilters()
                }
            }
            chipGroupCategory.addView(chip)
        }
    }

    private fun applyFilters() {
        val query = edtSearch.text?.toString()?.trim().orEmpty()
        var result = allProjects.filter { project ->
            (selectedCategory == CATEGORY_ALL || project.category == selectedCategory) &&
                (query.isEmpty() ||
                    project.name.contains(query, ignoreCase = true) ||
                    project.description.contains(query, ignoreCase = true) ||
                    project.publisherName.contains(query, ignoreCase = true))
        }
        result = when (sortMode) {
            SortMode.NEWEST -> result.sortedByDescending { it.createdAt }
            SortMode.DOWNLOADS -> result.sortedByDescending { it.downloadCount }
            SortMode.NAME -> result.sortedBy { it.name.lowercase() }
        }
        adapter.submit(result)
        txtEmpty.visibility = if (result.isEmpty()) View.VISIBLE else View.GONE
    }

    private fun showDetailDialog(project: RinProject) {
        AlertDialog.Builder(this)
            .setTitle(project.name)
            .setMessage(
                getString(
                    R.string.project_gallery_detail_format,
                    project.publisherName,
                    project.category,
                    project.fileCount,
                    project.downloadCount,
                    project.description.ifBlank { getString(R.string.project_gallery_no_description) }
                )
            )
            .setPositiveButton(R.string.project_gallery_action_download) { _, _ -> confirmAndDownload(project) }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    /** يفتح مربّع حوار لتسمية النسخة المحلية الجديدة (افتراضياً بنفس اسم المشروع المنشور، مع
     *  اقتراح اسم بديل تلقائياً إن كان مستخدَماً محلياً بالفعل)، ثم يحمّل ويفكّ الأرشيف فعلياً. */
    private fun confirmAndDownload(project: RinProject) {
        if (!isOnline()) { showOfflineOverlay(); return }

        val input = EditText(this)
        input.setText(suggestLocalName(project.name))
        AlertDialog.Builder(this)
            .setTitle(R.string.project_gallery_download_title)
            .setMessage(R.string.project_gallery_download_message)
            .setView(input)
            .setPositiveButton(R.string.project_gallery_action_download) { _, _ ->
                performDownload(project, input.text.toString())
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun suggestLocalName(baseName: String): String {
        val existing = ProjectManager.listProjects(this).map { it.name }.toSet()
        if (baseName !in existing) return baseName
        var counter = 2
        while ("$baseName ($counter)" in existing) counter++
        return "$baseName ($counter)"
    }

    private fun performDownload(project: RinProject, localName: String) {
        try {
            val newProject = ProjectManager.createProject(this, localName)
            val zipBytes = Base64.decode(project.zipBase64, Base64.NO_WRAP)
            ProjectManager.importProjectZipBytes(newProject, zipBytes)
            RinProjectRepository.incrementDownloadCount(project.id)
            Toast.makeText(this, getString(R.string.project_gallery_download_success, localName), Toast.LENGTH_SHORT).show()
            val intent = Intent(this, FilesActivity::class.java)
            intent.putExtra(FilesActivity.EXTRA_PROJECT_NAME, newProject.name)
            startActivity(intent)
        } catch (e: IllegalArgumentException) {
            Toast.makeText(this, e.message, Toast.LENGTH_SHORT).show()
        } catch (t: Throwable) {
            Toast.makeText(this, t.message ?: getString(R.string.project_gallery_download_failed), Toast.LENGTH_LONG).show()
        }
    }

    private inner class GalleryAdapter(
        val onOpenDetail: (RinProject) -> Unit,
        val onDownload: (RinProject) -> Unit
    ) : RecyclerView.Adapter<GalleryAdapter.VH>() {

        private var items: List<RinProject> = emptyList()

        fun submit(newItems: List<RinProject>) {
            items = newItems
            notifyDataSetChanged()
        }

        inner class VH(view: View) : RecyclerView.ViewHolder(view) {
            val root: View = view.findViewById(R.id.rootProjectCard)
            val imgThumbnail: ImageView = view.findViewById(R.id.imgProjectThumbnail)
            val txtInitial: TextView = view.findViewById(R.id.txtProjectGalleryInitial)
            val txtName: TextView = view.findViewById(R.id.txtProjectGalleryName)
            val txtMeta: TextView = view.findViewById(R.id.txtProjectGalleryMeta)
            val txtCategory: TextView = view.findViewById(R.id.txtProjectGalleryCategory)
            val txtDescription: TextView = view.findViewById(R.id.txtProjectGalleryDescription)
            val btnDownload: View = view.findViewById(R.id.btnProjectGalleryDownload)
        }

        override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): VH {
            val view = LayoutInflater.from(parent.context).inflate(R.layout.item_rin_project, parent, false)
            return VH(view)
        }

        override fun onBindViewHolder(holder: VH, position: Int) {
            val project = items[position]
            if (project.thumbnailBase64.isNotBlank()) {
                try {
                    val bytes = Base64.decode(project.thumbnailBase64, Base64.NO_WRAP)
                    val bitmap = BitmapFactory.decodeByteArray(bytes, 0, bytes.size)
                    holder.imgThumbnail.setImageBitmap(bitmap)
                    holder.imgThumbnail.visibility = View.VISIBLE
                    holder.txtInitial.visibility = View.GONE
                } catch (t: Throwable) {
                    holder.imgThumbnail.visibility = View.GONE
                    holder.txtInitial.visibility = View.VISIBLE
                    holder.txtInitial.text = project.name.take(1).uppercase()
                }
            } else {
                holder.imgThumbnail.visibility = View.GONE
                holder.txtInitial.visibility = View.VISIBLE
                holder.txtInitial.text = project.name.take(1).uppercase()
            }

            holder.txtName.text = "${project.name} — ${project.publisherName}"
            holder.txtMeta.text = getString(
                R.string.project_gallery_item_meta_format, project.fileCount, project.downloadCount
            )
            holder.txtCategory.text = project.category
            holder.txtDescription.text = project.description
            holder.txtDescription.visibility = if (project.description.isBlank()) View.GONE else View.VISIBLE

            holder.btnDownload.setOnClickListener { onDownload(project) }
            holder.root.setOnClickListener { onOpenDetail(project) }
        }

        override fun getItemCount(): Int = items.size
    }
}
