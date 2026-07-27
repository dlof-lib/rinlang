package com.dlof.rinlang.store

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.view.View
import android.widget.Button
import android.widget.EditText
import android.widget.ProgressBar
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import com.dlof.rinlang.ProjectManager
import com.dlof.rinlang.R
import com.dlof.rinlang.auth.AuthRepository
import com.dlof.rinlang.auth.LoginActivity
import com.google.android.material.chip.Chip
import com.google.android.material.chip.ChipGroup

class PublishPackageActivity : AppCompatActivity() {

    companion object {
        const val EXTRA_PROJECT_NAME = "extra_project_name"
        const val EXTRA_LIBRARY_NAME = "extra_library_name"

        /** تصنيفات جاهزة يختار الناشر منها واحداً عند نشر حزمة. */
        val CATEGORIES = listOf("عام", "رياضيات", "نصوص وسلاسل", "بيانات", "شبكة", "أدوات مساعدة")
    }

    private var selectedAssetUris: List<Uri> = emptyList()
    private var selectedCategory: String = CATEGORIES.first()

    private lateinit var edtName: EditText
    private lateinit var edtVersion: EditText
    private lateinit var edtDescription: EditText
    private lateinit var edtDependencies: EditText
    private lateinit var chipGroupCategory: ChipGroup
    private lateinit var txtAssetsSelected: TextView
    private lateinit var btnPublish: Button
    private lateinit var progress: ProgressBar

    private val pickAssetsLauncher =
        registerForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
            selectedAssetUris = uris
            txtAssetsSelected.text = getString(R.string.assets_selected_format, uris.size)
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // النشر يتطلب حساباً موثّقاً — نتحقق قبل حتى بناء الواجهة.
        val uid = AuthRepository.currentUid()
        if (uid == null) {
            Toast.makeText(this, R.string.publish_requires_login, Toast.LENGTH_LONG).show()
            startActivity(Intent(this, LoginActivity::class.java))
            finish()
            return
        }

        setContentView(R.layout.activity_publish_package)

        val projectName = intent.getStringExtra(EXTRA_PROJECT_NAME) ?: run { finish(); return }
        val libraryName = intent.getStringExtra(EXTRA_LIBRARY_NAME) ?: run { finish(); return }
        val project = ProjectManager.listProjects(this).find { it.name == projectName }
            ?: run { finish(); return }
        val library = ProjectManager.listLibraries(project).find { it.name == libraryName }
            ?: run { finish(); return }

        findViewById<TextView>(R.id.txtToolbarTitle).text = getString(R.string.publish_title)
        findViewById<View>(R.id.btnToolbarBack).setOnClickListener { finish() }

        edtName = findViewById(R.id.edtPackageName)
        edtVersion = findViewById(R.id.edtPackageVersion)
        edtDescription = findViewById(R.id.edtPackageDescription)
        edtDependencies = findViewById(R.id.edtPackageDependencies)
        chipGroupCategory = findViewById(R.id.chipGroupCategory)
        txtAssetsSelected = findViewById(R.id.txtAssetsSelected)
        btnPublish = findViewById(R.id.btnPublish)
        progress = findViewById(R.id.progressPublish)

        edtName.setText(library.name.removeSuffix(".og.rin"))
        edtVersion.setText("1.0.0")

        CATEGORIES.forEach { category ->
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

        findViewById<View>(R.id.btnPickAssets).setOnClickListener {
            pickAssetsLauncher.launch(arrayOf("image/*"))
        }

        btnPublish.setOnClickListener {
            val name = edtName.text.toString().trim()
            val version = edtVersion.text.toString().trim().ifBlank { "1.0.0" }
            val description = edtDescription.text.toString().trim()

            if (name.isEmpty()) {
                Toast.makeText(this, R.string.error_required_fields, Toast.LENGTH_SHORT).show()
                return@setOnClickListener
            }

            val dependencies = parseDependencies(edtDependencies.text.toString())

            setLoading(true)
            AuthRepository.fetchProfile(uid) { profile ->
                val publisherName = profile?.name?.ifBlank { profile.username } ?: "مستخدم Rin"
                try {
                    val zip = PackagingUtils.buildPackageZip(
                        context = this,
                        libraryFile = library.file,
                        packageName = name,
                        version = version,
                        description = description,
                        publisherName = publisherName,
                        license = "MIT",
                        extraAssetUris = selectedAssetUris,
                        dependencies = dependencies
                    )
                    val base64 = PackagingUtils.encodeFileToBase64(zip)
                    val fileName = "$name.${PackagingUtils.PACKAGE_EXTENSION}"

                    PackageRepository.publishPackage(
                        name = name,
                        version = version,
                        description = description,
                        license = "MIT",
                        publisherUid = uid,
                        publisherName = publisherName,
                        fileName = fileName,
                        base64Data = base64,
                        category = selectedCategory,
                        dependencies = dependencies
                    ) { success, error ->
                        setLoading(false)
                        if (success) {
                            Toast.makeText(this, R.string.publish_success, Toast.LENGTH_SHORT).show()
                            finish()
                        } else {
                            Toast.makeText(this, error ?: "فشل النشر", Toast.LENGTH_LONG).show()
                        }
                    }
                } catch (t: Throwable) {
                    setLoading(false)
                    Toast.makeText(this, t.message ?: "فشل تجهيز الحزمة", Toast.LENGTH_LONG).show()
                }
            }
        }
    }

    private fun setLoading(loading: Boolean) {
        progress.visibility = if (loading) View.VISIBLE else View.GONE
        btnPublish.isEnabled = !loading
    }

    /** يحوّل "name:^1.0.0, other:2.0.0" إلى خريطة تبعيات، متجاهلاً المدخلات الفارغة/الخاطئة. */
    private fun parseDependencies(raw: String): Map<String, String> =
        raw.split(",", "\n")
            .mapNotNull { entry ->
                val parts = entry.trim().split(":", limit = 2)
                if (parts.size == 2 && parts[0].isNotBlank() && parts[1].isNotBlank()) {
                    parts[0].trim() to parts[1].trim()
                } else null
            }
            .toMap()
}
