package com.dlof.rinlang.store.projects

import android.content.Context
import android.content.Intent
import android.graphics.BitmapFactory
import android.os.Bundle
import android.util.Base64
import android.view.View
import android.widget.Button
import android.widget.EditText
import android.widget.ImageView
import android.widget.ProgressBar
import android.widget.TextView
import android.widget.Toast
import com.dlof.rinlang.Project
import com.dlof.rinlang.ProjectManager
import com.dlof.rinlang.R
import com.dlof.rinlang.auth.AuthRepository
import com.dlof.rinlang.auth.LoginActivity
import com.dlof.rinlang.network.BaseConnectivityActivity
import com.dlof.rinlang.store.PublishPolicy
import com.google.android.material.chip.Chip
import com.google.android.material.chip.ChipGroup

/**
 * شاشة "انشر مشروعك": يختار المستخدم أحد مشاريعه المحلية (راجع [ProjectManager.listProjects])
 * وينشره كاملاً في "معرض مشاريع Rin" — بلا أي قيد على عدد المتابعين (بخلاف نشر إضافة، راجع
 * [PublishPolicy.MIN_FOLLOWERS_TO_PUBLISH_EXTENSION]).
 *
 * الصورة المصغّرة لبطاقة المشروع في المعرض **ليست** صورة يرفعها المستخدم، بل تُولَّد تلقائياً
 * من الكود الحقيقي لأحد ملفات .rin داخل المشروع المختار عبر [CodeThumbnailGenerator]، وتُعرَض
 * فوراً كمعاينة عند اختيار المشروع.
 */
class PublishRinProjectActivity : BaseConnectivityActivity() {

    companion object {
        /** اسم المشروع المحلي لتحديده مسبقاً عند فتح الشاشة من زر "نشر" داخل بطاقة مشروع معيّن. */
        const val EXTRA_PROJECT_NAME = "extra_project_name"

        fun start(context: Context, preselectedProjectName: String? = null) {
            val intent = Intent(context, PublishRinProjectActivity::class.java)
            if (preselectedProjectName != null) intent.putExtra(EXTRA_PROJECT_NAME, preselectedProjectName)
            context.startActivity(intent)
        }
    }

    private val categories = listOf("عام", "لعبة", "أداة", "موقع", "تجربة", "تعليمي")

    private lateinit var chipGroupProjects: ChipGroup
    private lateinit var chipGroupCategory: ChipGroup
    private lateinit var edtDescription: EditText
    private lateinit var imgThumbnailPreview: ImageView
    private lateinit var txtThumbnailSource: TextView
    private lateinit var txtNoProjects: View
    private lateinit var btnPublish: Button
    private lateinit var progress: ProgressBar

    private var localProjects: List<Project> = emptyList()
    private var selectedProject: Project? = null
    private var selectedCategory: String = categories.first()
    /** نتيجة توليد الصورة المصغّرة (base64, اسم الملف المصدر) للمشروع المختار حالياً. */
    private var generatedThumbnail: Pair<String, String>? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val uid = AuthRepository.currentUid()
        if (uid == null) {
            Toast.makeText(this, R.string.publish_requires_login, Toast.LENGTH_LONG).show()
            startActivity(Intent(this, LoginActivity::class.java))
            finish()
            return
        }

        setContentView(R.layout.activity_publish_rin_project)

        findViewById<TextView>(R.id.txtToolbarTitle).text = getString(R.string.project_publish_title)
        findViewById<View>(R.id.btnToolbarBack).setOnClickListener { finish() }

        chipGroupProjects = findViewById(R.id.chipGroupPublishProjectPicker)
        chipGroupCategory = findViewById(R.id.chipGroupPublishProjectCategory)
        edtDescription = findViewById(R.id.edtPublishProjectDescription)
        imgThumbnailPreview = findViewById(R.id.imgPublishProjectThumbnail)
        txtThumbnailSource = findViewById(R.id.txtPublishProjectThumbnailSource)
        txtNoProjects = findViewById(R.id.txtPublishProjectNone)
        btnPublish = findViewById(R.id.btnPublishProject)
        progress = findViewById(R.id.progressPublishProject)

        buildCategoryChips()

        localProjects = ProjectManager.listProjects(this)
        val preselected = intent.getStringExtra(EXTRA_PROJECT_NAME)
        if (localProjects.isEmpty()) {
            txtNoProjects.visibility = View.VISIBLE
            btnPublish.isEnabled = false
        } else {
            buildProjectChips(preselected)
        }

        btnPublish.setOnClickListener { attemptPublish(uid) }
    }

    private fun buildCategoryChips() {
        categories.forEach { category ->
            val chip = Chip(this).apply {
                text = category
                isCheckable = true
                isChecked = category == selectedCategory
                setOnClickListener {
                    selectedCategory = category
                    for (i in 0 until chipGroupCategory.childCount) {
                        (chipGroupCategory.getChildAt(i) as? Chip)?.isChecked = false
                    }
                    isChecked = true
                }
            }
            chipGroupCategory.addView(chip)
        }
    }

    private fun buildProjectChips(preselectedName: String?) {
        localProjects.forEach { project ->
            val chip = Chip(this).apply {
                text = project.name
                isCheckable = true
                setOnClickListener {
                    for (i in 0 until chipGroupProjects.childCount) {
                        (chipGroupProjects.getChildAt(i) as? Chip)?.isChecked = false
                    }
                    isChecked = true
                    onProjectSelected(project)
                }
            }
            chipGroupProjects.addView(chip)
        }
        val toSelect = localProjects.find { it.name == preselectedName } ?: localProjects.first()
        val indexToSelect = localProjects.indexOf(toSelect)
        (chipGroupProjects.getChildAt(indexToSelect) as? Chip)?.isChecked = true
        onProjectSelected(toSelect)
    }

    /** يُستدعى عند اختيار مشروع محلي: يولّد فوراً صورته المصغّرة من كوده الحقيقي ويعرضها كمعاينة. */
    private fun onProjectSelected(project: Project) {
        selectedProject = project
        generatedThumbnail = CodeThumbnailGenerator.generateFromProject(project)
        val thumbnail = generatedThumbnail
        if (thumbnail == null) {
            imgThumbnailPreview.setImageDrawable(null)
            txtThumbnailSource.text = getString(R.string.project_publish_thumbnail_none)
        } else {
            val (base64, sourceFileName) = thumbnail
            val bytes = Base64.decode(base64, Base64.NO_WRAP)
            val bitmap = BitmapFactory.decodeByteArray(bytes, 0, bytes.size)
            imgThumbnailPreview.setImageBitmap(bitmap)
            txtThumbnailSource.text = getString(R.string.project_publish_thumbnail_source_format, sourceFileName)
        }
    }

    private fun attemptPublish(uid: String) {
        val project = selectedProject ?: return
        val description = edtDescription.text.toString().trim()

        if (!isOnline()) { showOfflineOverlay(); return }

        val descriptionCheck = PublishPolicy.validateDescription(description)
        if (descriptionCheck is PublishPolicy.PolicyResult.Denied) {
            Toast.makeText(this, descriptionCheck.message, Toast.LENGTH_LONG).show()
            return
        }

        setLoading(true)
        AuthRepository.fetchProfile(uid) { profile ->
            val publisherName = profile?.name?.ifBlank { profile.username } ?: "مستخدم Rin"
            try {
                val (zipBase64, rawSize) = ProjectPublishingUtils.buildZipBase64(this, project)

                val sizeCheck = PublishPolicy.validateSize(rawSize)
                if (sizeCheck is PublishPolicy.PolicyResult.Denied) {
                    setLoading(false)
                    Toast.makeText(this, sizeCheck.message, Toast.LENGTH_LONG).show()
                    return@fetchProfile
                }

                val thumbnail = generatedThumbnail
                val fileCount = ProjectManager.listFiles(project).size

                val rinProject = RinProject(
                    name = project.name,
                    description = description,
                    publisherUid = uid,
                    publisherName = publisherName,
                    fileCount = fileCount,
                    category = selectedCategory,
                    zipBase64 = zipBase64,
                    thumbnailBase64 = thumbnail?.first.orEmpty(),
                    thumbnailSourceFile = thumbnail?.second.orEmpty()
                )

                RinProjectRepository.publishProject(rinProject) { success, error, _ ->
                    setLoading(false)
                    if (success) {
                        Toast.makeText(this, R.string.project_publish_success, Toast.LENGTH_SHORT).show()
                        finish()
                    } else {
                        Toast.makeText(this, error ?: getString(R.string.project_publish_failed), Toast.LENGTH_LONG).show()
                    }
                }
            } catch (t: Throwable) {
                setLoading(false)
                Toast.makeText(this, t.message ?: getString(R.string.project_publish_failed), Toast.LENGTH_LONG).show()
            }
        }
    }

    private fun setLoading(loading: Boolean) {
        progress.visibility = if (loading) View.VISIBLE else View.GONE
        btnPublish.isEnabled = !loading
    }
}
