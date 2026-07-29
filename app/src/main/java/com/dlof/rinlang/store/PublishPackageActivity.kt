package com.dlof.rinlang.store

import android.app.AlertDialog
import android.content.Intent
import android.graphics.BitmapFactory
import android.net.Uri
import android.os.Bundle
import android.view.View
import android.widget.Button
import android.widget.EditText
import android.widget.ImageView
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

        /** أقصى بُعد للصورة المصغّرة (thumbnail) المرفوعة — أكبر من الأيقونة لأنها بانر عريض. */
        private const val THUMBNAIL_MAX_DIMENSION = 800
    }

    private lateinit var library: com.dlof.rinlang.RinLibrary
    private var selectedAssetUris: List<Uri> = emptyList()
    private var selectedCategory: String = CATEGORIES.first()
    /** ملف README.md اختاره الناشر يدوياً؛ null يعني توليده تلقائياً عند النشر. */
    private var selectedReadmeUri: Uri? = null
    /** ملف ترخيص اختاره الناشر يدوياً؛ null يعني توليد قالب MIT تلقائياً عند النشر. */
    private var selectedLicenseUri: Uri? = null
    /** أيقونة الحزمة (اختيارية) بعد تصغيرها وترميزها base64 — فارغة يعني: بلا أيقونة مخصّصة. */
    private var iconBase64: String = ""
    /** الصورة المصغّرة للحزمة (اختيارية) بعد تصغيرها وترميزها base64 — فارغة يعني: بلا صورة مصغّرة. */
    private var thumbnailBase64: String = ""

    private lateinit var edtName: EditText
    private lateinit var edtVersion: EditText
    private lateinit var edtDescription: EditText
    private lateinit var edtDependencies: EditText
    private lateinit var edtLicense: EditText
    private lateinit var chipGroupCategory: ChipGroup
    private lateinit var txtAssetsSelected: TextView
    private lateinit var txtReadmeSelected: TextView
    private lateinit var txtLicenseSelected: TextView
    private lateinit var imgIconPreview: ImageView
    private lateinit var imgThumbnailPreview: ImageView
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

    /** أيقونة الحزمة: صورة مربّعة صغيرة، تُقصّ دائرياً للمعاينة عبر [AvatarUtils] فوراً بعد اختيارها. */
    private val pickIconLauncher =
        registerForActivityResult(ActivityResultContracts.GetContent()) { uri ->
            if (uri == null) return@registerForActivityResult
            val bitmap = decodeBitmapFromUri(uri) ?: run {
                Toast.makeText(this, R.string.error_image_decode_failed, Toast.LENGTH_SHORT).show()
                return@registerForActivityResult
            }
            iconBase64 = AvatarUtils.resizeAndEncodeToBase64(bitmap, AvatarUtils.AVATAR_MAX_DIMENSION)
            imgIconPreview.setImageBitmap(AvatarUtils.decodeCircularAvatar(iconBase64))
            imgIconPreview.visibility = View.VISIBLE
        }

    /** الصورة المصغّرة (thumbnail): بانر عريض غير مقصوص دائرياً، تُعرَض كما هي كمعاينة مستطيلة. */
    private val pickThumbnailLauncher =
        registerForActivityResult(ActivityResultContracts.GetContent()) { uri ->
            if (uri == null) return@registerForActivityResult
            val bitmap = decodeBitmapFromUri(uri) ?: run {
                Toast.makeText(this, R.string.error_image_decode_failed, Toast.LENGTH_SHORT).show()
                return@registerForActivityResult
            }
            thumbnailBase64 = AvatarUtils.resizeAndEncodeToBase64(bitmap, THUMBNAIL_MAX_DIMENSION)
            imgThumbnailPreview.setImageBitmap(bitmap)
            imgThumbnailPreview.visibility = View.VISIBLE
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
        imgIconPreview = findViewById(R.id.imgPackageIconPreview)
        imgThumbnailPreview = findViewById(R.id.imgPackageThumbnailPreview)
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

        findViewById<View>(R.id.btnPickIcon).setOnClickListener {
            pickIconLauncher.launch("image/*")
        }

        findViewById<View>(R.id.btnPickThumbnail).setOnClickListener {
            pickThumbnailLauncher.launch("image/*")
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

            // سياسات النشر التي لا تحتاج شبكة (اسم/وصف/إصدار/تبعيات) تُتحقَّق محلياً فوراً،
            // قبل أي اتصال، حتى لا نُتعب الشبكة أو المستخدم بخطوات لاحقة ستُرفض أصلاً.
            var denied = false
            denyOrElse(PublishPolicy.validateName(name)) { denied = true }
            if (!denied) denyOrElse(PublishPolicy.validateDescription(description)) { denied = true }
            if (!denied) denyOrElse(PublishPolicy.validateVersionFormat(version)) { denied = true }
            if (!denied) denyOrElse(PublishPolicy.validateDependencies(name, dependencies)) { denied = true }
            if (denied) return@setOnClickListener

            setLoading(true)
            runNetworkPolicyChecksThenPublish(uid, name, version, description, license, dependencies)
        }
    }

    /**
     * يعرض رسالة [result] ويستدعي [onDenied] إن كانت مرفوضة، وإلا لا يفعل شيئاً. تُستخدَم لتقصير
     * سلسلة "تحقّق ثم أوقف عند أول رفض" في [btnPublish] دون تكرار نفس الشرط لكل سياسة.
     */
    private inline fun denyOrElse(result: PublishPolicy.PolicyResult, onDenied: () -> Unit) {
        if (result is PublishPolicy.PolicyResult.Denied) {
            Toast.makeText(this, result.message, Toast.LENGTH_LONG).show()
            onDenied()
        }
    }

    /**
     * يسلسل بقية سياسات النشر التي تحتاج قراءة من الشبكة، بالترتيب التالي:
     * توفّر الاسم → ترقّي الإصدار (مقابل كل الإصدارات السابقة بنفس الاسم) → تحذير تشابه الأسماء
     * مع حزم شهيرة (تحذير غير حاجز) → تحديد معدّل النشر (rate limiting) حسب ثقة الناشر. أي رفض
     * في أي خطوة يوقف السلسلة فوراً ويعرض رسالته.
     */
    private fun runNetworkPolicyChecksThenPublish(
        uid: String,
        name: String,
        version: String,
        description: String,
        license: String,
        dependencies: Map<String, String>
    ) {
        PackageRepository.isNameAvailable(name) { available ->
            if (!available) {
                setLoading(false)
                Toast.makeText(this, R.string.error_package_name_taken, Toast.LENGTH_LONG).show()
                return@isNameAvailable
            }
            PackageRepository.fetchExistingVersions(name) { existingVersions ->
                val progression = PublishPolicy.validateVersionProgression(version, existingVersions)
                if (progression is PublishPolicy.PolicyResult.Denied) {
                    setLoading(false)
                    Toast.makeText(this, progression.message, Toast.LENGTH_LONG).show()
                    return@fetchExistingVersions
                }
                PackageRepository.fetchAllPackages { allPackages ->
                    val similarNames = PublishPolicy.findSimilarPopularNames(name, allPackages)
                    if (similarNames.isNotEmpty()) {
                        setLoading(false)
                        confirmSimilarNameThenContinue(similarNames) {
                            setLoading(true)
                            checkRateLimitThenPublish(uid, name, version, description, license, dependencies)
                        }
                    } else {
                        checkRateLimitThenPublish(uid, name, version, description, license, dependencies)
                    }
                }
            }
        }
    }

    /** يتحقق من حصّة النشر خلال 24 ساعة حسب مستوى ثقة الناشر (راجع [PublisherBadgeUtils])، ثم ينشر إن كانت النتيجة مسموحة. */
    private fun checkRateLimitThenPublish(
        uid: String,
        name: String,
        version: String,
        description: String,
        license: String,
        dependencies: Map<String, String>
    ) {
        PackageRepository.fetchUserPackages(uid) { ownPackages ->
            val rateLimit = PublishPolicy.checkRateLimit(
                recentPublishTimestamps = ownPackages.map { it.createdAt },
                isVerifiedPublisher = PublisherBadgeUtils.isEligible(ownPackages)
            )
            if (rateLimit is PublishPolicy.PolicyResult.Denied) {
                setLoading(false)
                Toast.makeText(this, rateLimit.message, Toast.LENGTH_LONG).show()
                return@fetchUserPackages
            }
            publishNow(uid, name, version, description, license, dependencies)
        }
    }

    /**
     * يحذّر الناشر أن الاسم الذي اختاره قريب جداً (خوارزمية Levenshtein) من حزمة شهيرة موجودة
     * فعلاً، ويطلب تأكيداً صريحاً قبل المتابعة — تحذير وقائي ضد انتحال الأسماء (typosquatting)،
     * وليس رفضاً تلقائياً، فقد يكون الاسم مشروعاً تماماً.
     */
    private fun confirmSimilarNameThenContinue(similarNames: List<String>, onConfirmed: () -> Unit) {
        val namesList = similarNames.joinToString("، ")
        AlertDialog.Builder(this)
            .setTitle(R.string.similar_name_warning_title)
            .setMessage(getString(R.string.similar_name_warning_message, namesList))
            .setPositiveButton(R.string.action_publish_submit) { _, _ -> onConfirmed() }
            .setNegativeButton(R.string.cancel, null)
            .show()
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

                    val sizeCheck = PublishPolicy.validateSize(zip.length())
                    if (sizeCheck is PublishPolicy.PolicyResult.Denied) {
                        setLoading(false)
                        Toast.makeText(this, sizeCheck.message, Toast.LENGTH_LONG).show()
                        return@fetchProfile
                    }

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
                        dependencies = dependencies,
                        iconBase64 = iconBase64,
                        thumbnailBase64 = thumbnailBase64
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

    /** يفكّ [uri] (نتيجة منتقي الصور) إلى Bitmap، أو null إن تعذّر ذلك. */
    private fun decodeBitmapFromUri(uri: Uri): android.graphics.Bitmap? = try {
        contentResolver.openInputStream(uri)?.use { BitmapFactory.decodeStream(it) }
    } catch (t: Throwable) {
        null
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
