package com.dlof.rinlang.store.extensions

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.view.View
import android.widget.Button
import android.widget.CheckBox
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import com.dlof.rinlang.R
import com.dlof.rinlang.auth.AuthRepository
import com.dlof.rinlang.auth.LoginActivity
import com.dlof.rinlang.network.BaseConnectivityActivity
import com.dlof.rinlang.store.PublishPolicy
import com.google.android.material.chip.Chip
import com.google.android.material.chip.ChipGroup

/**
 * شاشة "انشر إضافتك": تتيح لأي مستخدم مسجَّل دخوله نشر إضافته الخاصة في "Rin Extensions
 * Marketplace" — دون حاجة لأي مراجعة يدوية مسبقة، بنفس فلسفة [com.dlof.rinlang.store.PublishPackageActivity]
 * لمتجر المكتبات: القراءة عامة للجميع، والنشر متاح لأي حساب موثَّق.
 *
 * الناشر يختار: الاسم، الإصدار، الوصف، النوع، اللغات المستخدَمة، الأذونات التي تطلبها إضافته
 * (تُعرَض لاحقاً كشاشة أمان قبل تثبيت أي مستخدم لها)، ملفات محتوى الإضافة نفسها (تُضغَط تلقائياً
 * zip)، ولقطات شاشة اختيارية. يُحسَب توقيع رقمي (بصمة سلامة) للمحتوى تلقائياً عند النشر.
 */
class PublishExtensionActivity : BaseConnectivityActivity() {

    companion object {
        /** يفتح شاشة نشر إضافة جديدة. لا تحتاج أي بيانات إضافية—فالإضافة مستقلة عن أي مشروع. */
        fun start(context: Context) {
            context.startActivity(Intent(context, PublishExtensionActivity::class.java))
        }
    }

    private lateinit var edtName: EditText
    private lateinit var edtVersion: EditText
    private lateinit var edtDescription: EditText
    private lateinit var edtLanguages: EditText
    private lateinit var chipGroupType: ChipGroup
    private lateinit var containerPermissions: LinearLayout
    private lateinit var txtContentFilesSelected: TextView
    private lateinit var txtScreenshotsSelected: TextView
    private lateinit var btnPublish: Button
    private lateinit var progress: ProgressBar

    private var selectedType: ExtensionType = ExtensionType.EXTENSION
    private var selectedContentUris: List<Uri> = emptyList()
    private var selectedScreenshotUris: List<Uri> = emptyList()
    private val selectedPermissionIds = mutableSetOf<String>()

    private val pickContentLauncher =
        registerForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
            if (uris.isEmpty()) return@registerForActivityResult
            selectedContentUris = uris
            txtContentFilesSelected.text = getString(R.string.ext_publish_files_selected_format, uris.size)
            txtContentFilesSelected.visibility = View.VISIBLE
        }

    private val pickScreenshotsLauncher =
        registerForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
            if (uris.isEmpty()) return@registerForActivityResult
            selectedScreenshotUris = uris.take(5)
            txtScreenshotsSelected.text = getString(R.string.ext_publish_screenshots_selected_format, selectedScreenshotUris.size)
            txtScreenshotsSelected.visibility = View.VISIBLE
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val uid = AuthRepository.currentUid()
        if (uid == null) {
            Toast.makeText(this, R.string.publish_requires_login, Toast.LENGTH_LONG).show()
            startActivity(Intent(this, LoginActivity::class.java))
            finish()
            return
        }

        setContentView(R.layout.activity_publish_extension)

        findViewById<TextView>(R.id.txtToolbarTitle).text = getString(R.string.ext_publish_title)
        findViewById<View>(R.id.btnToolbarBack).setOnClickListener { finish() }

        edtName = findViewById(R.id.edtExtPublishName)
        edtVersion = findViewById(R.id.edtExtPublishVersion)
        edtDescription = findViewById(R.id.edtExtPublishDescription)
        edtLanguages = findViewById(R.id.edtExtPublishLanguages)
        chipGroupType = findViewById(R.id.chipGroupExtPublishType)
        containerPermissions = findViewById(R.id.containerExtPublishPermissions)
        txtContentFilesSelected = findViewById(R.id.txtExtPublishFilesSelected)
        txtScreenshotsSelected = findViewById(R.id.txtExtPublishScreenshotsSelected)
        btnPublish = findViewById(R.id.btnPublishExtension)
        progress = findViewById(R.id.progressPublishExtension)

        edtVersion.setText("1.0.0")

        buildTypeChips()
        buildPermissionCheckboxes()

        findViewById<View>(R.id.btnPickExtContentFiles).setOnClickListener {
            pickContentLauncher.launch(arrayOf("*/*"))
        }
        findViewById<View>(R.id.btnPickExtScreenshots).setOnClickListener {
            pickScreenshotsLauncher.launch(arrayOf("image/*"))
        }

        btnPublish.setOnClickListener { attemptPublish(uid) }
    }

    private fun typeLabel(type: ExtensionType): String = when (type) {
        ExtensionType.EXTENSION -> getString(R.string.ext_type_extension)
        ExtensionType.LIBRARY -> getString(R.string.ext_type_library)
        ExtensionType.THEME -> getString(R.string.ext_type_theme)
        ExtensionType.UI_COMPONENT -> getString(R.string.ext_type_ui_component)
        ExtensionType.DEBUG_TOOL -> getString(R.string.ext_type_debug_tool)
        ExtensionType.AI_TOOL -> getString(R.string.ext_type_ai_tool)
        ExtensionType.TEMPLATE -> getString(R.string.ext_type_template)
    }

    private fun buildTypeChips() {
        ExtensionType.values().forEach { type ->
            val chip = Chip(this).apply {
                text = typeLabel(type)
                isCheckable = true
                isChecked = type == selectedType
                setOnClickListener {
                    selectedType = type
                    for (i in 0 until chipGroupType.childCount) {
                        (chipGroupType.getChildAt(i) as? Chip)?.isChecked = false
                    }
                    isChecked = true
                }
            }
            chipGroupType.addView(chip)
        }
    }

    /** يبني قائمة اختيار الأذونات (CheckBox واحد لكل إذن في الكتالوج) — الناشر يحدّد بصراحة
     *  ما تطلبه إضافته فعلياً، وهذا هو ما سيُعرَض لاحقاً في شاشة الأمان قبل أي تثبيت. */
    private fun buildPermissionCheckboxes() {
        ExtensionPermissions.CATALOG.forEach { permission ->
            val checkbox = CheckBox(this).apply {
                text = permission.label
                setTextColor(getColorCompat(R.color.rin_editor_text))
                textSize = 13f
                setOnCheckedChangeListener { _, checked ->
                    if (checked) selectedPermissionIds.add(permission.id) else selectedPermissionIds.remove(permission.id)
                }
            }
            containerPermissions.addView(checkbox)
        }
    }

    private fun getColorCompat(colorRes: Int): Int = androidx.core.content.ContextCompat.getColor(this, colorRes)

    private fun attemptPublish(uid: String) {
        val name = edtName.text.toString().trim()
        val version = edtVersion.text.toString().trim().ifBlank { "1.0.0" }
        val description = edtDescription.text.toString().trim()
        val languages = edtLanguages.text.toString().split(",", "،")
            .map { it.trim() }.filter { it.isNotEmpty() }

        if (name.isEmpty()) {
            Toast.makeText(this, R.string.error_required_fields, Toast.LENGTH_SHORT).show()
            return
        }
        if (selectedContentUris.isEmpty()) {
            Toast.makeText(this, R.string.ext_publish_content_required, Toast.LENGTH_LONG).show()
            return
        }
        if (!isOnline()) { showOfflineOverlay(); return }

        var denied = false
        denyOrElse(PublishPolicy.validateName(name.lowercase().replace(" ", "-"))) { denied = true }
        if (!denied) denyOrElse(PublishPolicy.validateDescription(description)) { denied = true }
        if (!denied) denyOrElse(PublishPolicy.validateVersionFormat(version)) { denied = true }
        if (denied) return

        setLoading(true)
        AuthRepository.fetchProfile(uid) { profile ->
            val publisherName = profile?.name?.ifBlank { profile.username } ?: "مستخدم Rin"
            try {
                val zip = ExtensionPackagingUtils.buildExtensionZip(this, name, selectedContentUris)

                val sizeCheck = PublishPolicy.validateSize(zip.length())
                if (sizeCheck is PublishPolicy.PolicyResult.Denied) {
                    setLoading(false)
                    Toast.makeText(this, sizeCheck.message, Toast.LENGTH_LONG).show()
                    return@fetchProfile
                }

                val base64 = ExtensionPackagingUtils.encodeFileToBase64(zip)
                val screenshots = selectedScreenshotUris.mapNotNull { uri ->
                    ExtensionPackagingUtils.encodeScreenshot(this, uri)?.let { data -> ExtensionScreenshot(base64Data = data) }
                }

                val ext = RinExtension(
                    name = name,
                    version = version,
                    developer = publisherName,
                    developerUid = uid,
                    description = description,
                    type = selectedType.id,
                    permissions = selectedPermissionIds.toList(),
                    languages = languages,
                    screenshots = screenshots,
                    fileName = "$name.rinext",
                    base64Data = base64
                )

                ExtensionRepository.publishExtension(ext) { success, error, _ ->
                    setLoading(false)
                    if (success) {
                        Toast.makeText(this, R.string.ext_publish_success, Toast.LENGTH_SHORT).show()
                        finish()
                    } else {
                        Toast.makeText(this, error ?: getString(R.string.ext_publish_failed), Toast.LENGTH_LONG).show()
                    }
                }
            } catch (t: Throwable) {
                setLoading(false)
                Toast.makeText(this, t.message ?: getString(R.string.ext_publish_failed), Toast.LENGTH_LONG).show()
            }
        }
    }

    private inline fun denyOrElse(result: PublishPolicy.PolicyResult, onDenied: () -> Unit) {
        if (result is PublishPolicy.PolicyResult.Denied) {
            Toast.makeText(this, result.message, Toast.LENGTH_LONG).show()
            onDenied()
        }
    }

    private fun setLoading(loading: Boolean) {
        progress.visibility = if (loading) View.VISIBLE else View.GONE
        btnPublish.isEnabled = !loading
    }
}
