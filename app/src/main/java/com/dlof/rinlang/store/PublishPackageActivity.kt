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
import com.dlof.rinlang.network.BaseConnectivityActivity
import com.dlof.rinlang.ProjectManager
import com.dlof.rinlang.R
import com.dlof.rinlang.auth.AuthRepository
import com.dlof.rinlang.auth.LoginActivity
import com.google.android.material.chip.Chip
import com.google.android.material.chip.ChipGroup

class PublishPackageActivity : BaseConnectivityActivity() {

    companion object {
        const val EXTRA_PROJECT_NAME = "extra_project_name"
        const val EXTRA_LIBRARY_NAME = "extra_library_name"

        /** تصنيفات جاهزة يختار الناشر منها واحداً عند نشر حزمة. */
        val CATEGORIES = listOf("عام", "رياضيات", "نصوص وسلاسل", "بيانات", "شبكة", "أدوات مساعدة")
    }

    private lateinit var library: com.dlof.rinlang.RinLibrary
    private var selectedAssetUris: List<Uri> = emptyList()
    private var selectedCategory: String = CATEGORIES.first()
    /** ملف README.md اختاره الناشر يدوياً؛ null يعني توليده تلقائياً عند النشر. */
    private var selectedReadmeUri: Uri? = null
    /** ملف ترخيص اختاره الناشر يدوياً؛ null يعني توليد قالب MIT تلقائياً عند النشر. */
    private var selectedLicenseUri: Uri? = null

    private lateinit var edtName: EditText
    private lateinit var edtVersion: EditText
    private lateinit var edtDescription: EditText
    private lateinit var edtDependencies: EditText
    private lateinit var edtLicense: EditText
    private lateinit var chipGroupCategory: ChipGroup
    private lateinit var txtAssetsSelected: TextView
    private lateinit var txtReadmeSelected: TextView
    private lateinit var txtLicenseSelected: TextView
    private lateinit var btnPublish: Button
    private lateinit var progress: ProgressBar

    private val pickAssetsLauncher =
        registerForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
            selectedAssetUris = uris
            txtAssetsSelected.text = getString(R.string.assets_selected_format, uris.size)
        }

    private val pickReadmeLauncher =
        registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            if (uri == null) return@registerForActivityResult
            selectedReadmeUri = uri
            txtReadmeSelected.text = fileDisplayName(uri) ?: getString(R.string.file_selected_generic)
            txtReadmeSelected.visibility = View.VISIBLE
        }

    private val pickLicenseLauncher =
        registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            if (uri == null) return@registerForActivityResult
            selectedLicenseUri = uri
            txtLicenseSelected.text = fileDisplayName(uri) ?: getString(R.string.file_selected_generic)
            txtLicenseSelected.visibility = View.VISIBLE
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
        library = ProjectManager.listLibraries(project).find { it.name == libraryName }
            ?: run { finish(); return }

        findViewById<TextView>(R.id.txtToolbarTitle).text = getString(R.string.publish_title)
        findViewById<View>(R.id.btnToolbarBack).setOnClickListener { finish() }

        edtName = findViewById(R.id.edtPackageName)
        edtVersion = findViewById(R.id.edtPackageVersion)
        edtDescription = findViewById(R.id.edtPackageDescription)
        edtDependencies = findViewById(R.id.edtPackageDependencies)
        edtLicense = findViewById(R.id.edtPackageLicense)
        chipGroupCategory = findViewById(R.id.chipGroupCategory)
        txtAssetsSelected = findViewById(R.id.txtAssetsSelected)
        txtReadmeSelected = findViewById(R.id.txtReadmeSelected)
        txtLicenseSelected = findViewById(R.id.txtLicenseSelected)
        btnPublish = findViewById(R.id.btnPublish)
        progress = findViewById(R.id.progressPublish)

        edtName.setText(library.name.removeSuffix(".og.rin"))
        edtVersion.setText("1.0.0")
        edtLicense.setText("MIT")

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

        findViewById<View>(R.id.btnPickReadme).setOnClickListener {
            pickReadmeLauncher.launch(arrayOf("text/markdown", "text/plain", "text/*"))
        }

        findViewById<View>(R.id.btnPickLicense).setOnClickListener {
            pickLicenseLauncher.launch(arrayOf("text/plain", "text/*", "application/octet-stream"))
        }

        btnPublish.setOnClickListener {
            val name = edtName.text.toString().trim()
            val version = edtVersion.text.toString().trim().ifBlank { "1.0.0" }
            val description = edtDescription.text.toString().trim()
            val license = edtLicense.text.toString().trim().ifBlank { "MIT" }

            if (name.isEmpty()) {
                Toast.makeText(this, R.string.error_required_fields, Toast.LENGTH_SHORT).show()
                return@setOnClickListener
            }
            if (!isOnline()) { showOfflineOverlay(); return@setOnClickListener }

            val dependencies = parseDependencies(edtDependencies.text.toString())

            setLoading(true)
            // منع تشابه الأسماء بين كل المستخدمين (وليس فقط تكرار حرفي): يُتحقَّق قبل حتى تجهيز
            // ملف الحزمة، حتى لا نُضيّع وقت المستخدم في ضغط/ترميز حزمة سيُرفض نشرها لاحقاً.
            PackageRepository.isNameAvailable(name) { available ->
                if (!available) {
                    setLoading(false)
                    Toast.makeText(this, R.string.error_package_name_taken, Toast.LENGTH_LONG).show()
                    return@isNameAvailable
                }
                publishNow(uid, name, version, description, license, dependencies)
            }
        }
    }

    private fun publishNow(
        uid: String,
        name: String,
        version: String,
        description: String,
        license: String,
        dependencies: Map<String, String>
    ) {
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
                        license = license,
                        extraAssetUris = selectedAssetUris,
                        dependencies = dependencies,
                        customReadmeUri = selectedReadmeUri,
                        customLicenseUri = selectedLicenseUri
                    )
                    val base64 = PackagingUtils.encodeFileToBase64(zip)
                    val fileName = "$name.${PackagingUtils.PACKAGE_EXTENSION}"

                    PackageRepository.publishPackage(
                        name = name,
                        version = version,
                        description = description,
                        license = license,
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

    private fun setLoading(loading: Boolean) {
        progress.visibility = if (loading) View.VISIBLE else View.GONE
        btnPublish.isEnabled = !loading
    }

    /** اسم عرض الملف المُختار عبر SAF (لعرض اسم README/LICENSE بعد اختياره)، أو null إن تعذّر ذلك. */
    private fun fileDisplayName(uri: Uri): String? = try {
        contentResolver.query(uri, arrayOf(android.provider.OpenableColumns.DISPLAY_NAME), null, null, null)
            ?.use { cursor ->
                if (cursor.moveToFirst()) {
                    val idx = cursor.getColumnIndex(android.provider.OpenableColumns.DISPLAY_NAME)
                    if (idx >= 0) cursor.getString(idx) else null
                } else null
            }
    } catch (t: Throwable) {
        null
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
