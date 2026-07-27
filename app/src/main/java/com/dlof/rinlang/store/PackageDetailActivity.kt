package com.dlof.rinlang.store

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.os.Build
import android.os.Bundle
import android.text.Html
import android.util.Base64
import android.view.LayoutInflater
import android.view.View
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.TextView
import com.dlof.rinlang.R
import com.dlof.rinlang.auth.AuthRepository
import com.dlof.rinlang.network.BaseConnectivityActivity

/**
 * صفحة "تفاصيل الحزمة": تعرض حزمة واحدة من متجر Rin بشكل شبيه بصفحة مستودع على GitHub —
 * رأس بصورة واسم الناشر (+ شارة توثيق إن كان مؤهَّلاً)، محتوى README.md مُصيَّر، نص الترخيص،
 * وقائمة كل ملفات الحزمة. لا تحتاج اتصالاً بالشبكة لعرض المحتوى نفسه (كله مخزَّن أصلاً داخل
 * [RinPackage.base64Data] الذي وصل مع قائمة المتجر)، إلا لجلب صورة الملف الشخصي للناشر ولتثبيت
 * الحزمة فعلياً.
 */
class PackageDetailActivity : BaseConnectivityActivity() {

    companion object {
        const val EXTRA_PACKAGE = "extra_package"
        /** يُمرَّر من شاشة المتجر (التي حسبت التأهل لكل الناشرين دفعة واحدة) لتفادي إعادة الحساب هنا. */
        const val EXTRA_PUBLISHER_VERIFIED = "extra_publisher_verified"
        /** معرّف الحزمة (RinPackage.id) التي ضغط المستخدم زر تثبيتها من داخل هذه الشاشة. */
        const val EXTRA_SELECTED_PACKAGE_ID = "extra_selected_package_id"
    }

    private lateinit var pkg: RinPackage

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_package_detail)

        @Suppress("DEPRECATION")
        val received = intent.getSerializableExtra(EXTRA_PACKAGE) as? RinPackage
        if (received == null) { finish(); return }
        pkg = received

        findViewById<View>(R.id.btnToolbarBack).setOnClickListener { finish() }
        findViewById<TextView>(R.id.txtToolbarTitle).text = pkg.name

        bindHeader()
        bindPublisher()
        bindReadmeAndLicense()
        bindFiles()

        // لا مشروع محدَّد داخل هذه الشاشة (شاشة استعراض فقط)؛ التثبيت الفعلي (مع التحقق من
        // التبعيات) يبقى مسؤولية شاشة المتجر التي فتحت هذه الشاشة، فقط نُعيد معرّف الحزمة إليها.
        findViewById<View>(R.id.btnDetailInstall).setOnClickListener {
            val result = android.content.Intent()
            result.putExtra(EXTRA_SELECTED_PACKAGE_ID, pkg.id)
            setResult(RESULT_OK, result)
            finish()
        }
    }

    override fun onConnectionRestored() {
        bindPublisher()
    }

    private fun bindHeader() {
        findViewById<TextView>(R.id.txtDetailPackageName).text = pkg.name
        val txtDescription = findViewById<TextView>(R.id.txtDetailDescription)
        txtDescription.text = pkg.description
        txtDescription.visibility = if (pkg.description.isBlank()) View.GONE else View.VISIBLE

        findViewById<TextView>(R.id.txtDetailMeta).text = getString(
            R.string.package_detail_meta_format,
            pkg.version, pkg.license, pkg.downloadCount, pkg.averageRating, pkg.ratingCount
        )
    }

    private fun bindPublisher() {
        val txtName = findViewById<TextView>(R.id.txtDetailPublisherName)
        val txtInitial = findViewById<TextView>(R.id.txtDetailPublisherInitial)
        val imgAvatar = findViewById<ImageView>(R.id.imgDetailPublisherAvatar)
        val imgBadge = findViewById<ImageView>(R.id.imgDetailVerifiedBadge)

        txtName.text = pkg.publisherName
        txtInitial.text = pkg.publisherName.take(1).uppercase()

        // الشارة: إن مرّرتها شاشة المتجر جاهزة (من allPackages المحمَّلة أصلاً) نستخدمها فوراً بلا
        // أي طلب شبكة إضافي؛ وإلا (مثلاً فُتحت الشاشة من مصدر آخر مستقبلاً) نحسبها بجلب حزم الناشر.
        if (intent.hasExtra(EXTRA_PUBLISHER_VERIFIED)) {
            imgBadge.visibility = if (intent.getBooleanExtra(EXTRA_PUBLISHER_VERIFIED, false)) View.VISIBLE else View.GONE
        } else {
            PackageRepository.fetchUserPackages(pkg.publisherUid) { packages ->
                imgBadge.visibility = if (PublisherBadgeUtils.isEligible(packages)) View.VISIBLE else View.GONE
            }
        }

        AuthRepository.fetchProfile(pkg.publisherUid) { profile ->
            val avatarBase64 = profile?.avatarBase64
            if (avatarBase64.isNullOrBlank()) {
                imgAvatar.setImageDrawable(null)
                txtInitial.visibility = View.VISIBLE
                return@fetchProfile
            }
            try {
                val bytes = Base64.decode(avatarBase64, Base64.NO_WRAP)
                val bitmap: Bitmap? = BitmapFactory.decodeByteArray(bytes, 0, bytes.size)
                if (bitmap != null) {
                    imgAvatar.setImageBitmap(bitmap)
                    txtInitial.visibility = View.GONE
                } else {
                    txtInitial.visibility = View.VISIBLE
                }
            } catch (t: Throwable) {
                txtInitial.visibility = View.VISIBLE
            }
        }
    }

    private fun bindReadmeAndLicense() {
        val contents = PackagingUtils.readContents(pkg)

        val txtReadme = findViewById<TextView>(R.id.txtDetailReadme)
        val readme = contents.readme
        if (readme.isNullOrBlank()) {
            txtReadme.text = getString(R.string.package_detail_no_readme)
        } else {
            val html = MarkdownLite.toHtml(readme)
            txtReadme.text = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                Html.fromHtml(html, Html.FROM_HTML_MODE_COMPACT)
            } else {
                @Suppress("DEPRECATION")
                Html.fromHtml(html)
            }
        }

        val txtLicenseTitle = findViewById<TextView>(R.id.txtDetailLicenseTitle)
        val txtLicense = findViewById<TextView>(R.id.txtDetailLicense)
        val license = contents.license
        if (license.isNullOrBlank()) {
            txtLicenseTitle.visibility = View.GONE
            txtLicense.visibility = View.GONE
        } else {
            txtLicenseTitle.visibility = View.VISIBLE
            txtLicense.visibility = View.VISIBLE
            txtLicense.text = license
        }
    }

    private fun bindFiles() {
        val container = findViewById<LinearLayout>(R.id.containerDetailFiles)
        container.removeAllViews()
        val contents = PackagingUtils.readContents(pkg)
        val inflater = LayoutInflater.from(this)
        for (file in contents.files) {
            val row = inflater.inflate(R.layout.item_package_file, container, false)
            row.findViewById<TextView>(R.id.txtFileName).text = file.name
            row.findViewById<TextView>(R.id.txtFileSize).text = formatSize(file.sizeBytes)
            container.addView(row)
        }
    }

    private fun formatSize(bytes: Long): String = when {
        bytes < 1024 -> "$bytes B"
        bytes < 1024 * 1024 -> "${bytes / 1024} KB"
        else -> String.format("%.1f MB", bytes / (1024.0 * 1024.0))
    }
}
