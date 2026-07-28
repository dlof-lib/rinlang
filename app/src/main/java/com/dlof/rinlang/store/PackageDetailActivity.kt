package com.dlof.rinlang.store

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.os.Bundle
import android.util.Base64
import android.view.LayoutInflater
import android.view.View
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
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

    /** حالة الإعجاب المحلية (متفائلة): تُحدَّث فوراً عند الضغط قبل استلام تأكيد Firebase. */
    private var isLiked = false
    private var likeCount = 0L

    /** حالة الانتساب لناشر الحزمة (متفائلة أيضاً، بنفس أسلوب [isLiked]). */
    private var isSubscribed = false

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
        bindLike()
        bindSubscribe()

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

        findViewById<View>(R.id.rowDetailPublisher).setOnClickListener {
            PublicProfileActivity.start(this, pkg.publisherUid)
        }
        clipToCircle(imgAvatar)

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

    /** يقصّ [view] إلى دائرة كاملة، لعرض صورة الملف الشخصي كشارة دائرية بدل مربّع خام. */
    private fun clipToCircle(view: ImageView) {
        view.clipToOutline = true
        view.outlineProvider = object : android.view.ViewOutlineProvider() {
            override fun getOutline(v: View, outline: android.graphics.Outline) {
                outline.setOval(0, 0, v.width, v.height)
            }
        }
    }

    /**
     * يهيّئ زر الإعجاب (قلب): يعرض عدد الإعجابات الحالي فوراً من [pkg] المُمرَّرة أصلاً (بلا أي
     * طلب شبكة)، ثم يجلب هل المستخدم الحالي (إن سجّل الدخول) أعجب بها مسبقاً ليضبط شكل القلب
     * (مملوء/مفرَّغ) بدقة. الضغط يبدّل الحالة محلياً فوراً (تفاؤلي) ثم يُزامنها مع Firebase،
     * ويتراجع عن التغيير المحلي إن فشلت المزامنة.
     */
    private fun bindLike() {
        likeCount = pkg.likeCount
        renderLikeState()

        val uid = AuthRepository.currentUid()
        if (uid != null) {
            PackageRepository.fetchLikeState(pkg.id, uid) { liked ->
                isLiked = liked
                renderLikeState()
            }
        }

        findViewById<View>(R.id.btnDetailLike).setOnClickListener {
            if (!isOnline()) { showOfflineOverlay(); return@setOnClickListener }
            val currentUid = AuthRepository.currentUid()
            if (currentUid == null) {
                Toast.makeText(this, R.string.like_package_login_required, Toast.LENGTH_SHORT).show()
                return@setOnClickListener
            }

            // تحديث متفائل فوري: الواجهة تستجيب لحظياً، ثم تتراجع إن رجعت Firebase بفشل
            val previousLiked = isLiked
            val previousCount = likeCount
            isLiked = !isLiked
            likeCount = if (isLiked) likeCount + 1 else (likeCount - 1).coerceAtLeast(0L)
            renderLikeState()

            PackageRepository.toggleLike(pkg.id, currentUid) { liked, success ->
                if (!success) {
                    isLiked = previousLiked
                    likeCount = previousCount
                } else {
                    isLiked = liked
                }
                renderLikeState()
            }
        }
    }

    /** يعكس [isLiked] و[likeCount] الحاليَين على أيقونة القلب ولونها ونص العدّاد. */
    private fun renderLikeState() {
        val imgIcon = findViewById<ImageView>(R.id.imgDetailLikeIcon)
        val txtCount = findViewById<TextView>(R.id.txtDetailLikeCount)
        val tint = if (isLiked) getColor(R.color.rin_like_active) else getColor(R.color.rin_editor_hint)
        imgIcon.setImageResource(if (isLiked) R.drawable.ic_heart_filled else R.drawable.ic_heart_outline)
        imgIcon.imageTintList = android.content.res.ColorStateList.valueOf(tint)
        txtCount.setTextColor(tint)
        txtCount.text = getString(R.string.like_count_format, likeCount)
    }

    /**
     * يهيّئ زر "الانتساب" لناشر الحزمة: يبقى مخفياً إن لم يكن هناك مستخدم مسجَّل دخوله بعد أو
     * كان المستخدم الحالي هو ناشر الحزمة نفسه (لا معنى للانتساب للنفس)، ثم يجلب حالة الانتساب
     * الحالية ليضبط نص الزر (انتساب / تم الانتساب). الضغط يبدّل الحالة محلياً فوراً (تفاؤلي)
     * ثم يُزامنها مع Firebase، ويتراجع عن التغيير المحلي إن فشلت المزامنة — بنفس أسلوب [bindLike].
     */
    private fun bindSubscribe() {
        val btn = findViewById<TextView>(R.id.btnDetailSubscribe)
        val uid = AuthRepository.currentUid()

        if (uid == null || uid == pkg.publisherUid) {
            btn.visibility = View.GONE
            return
        }

        btn.visibility = View.VISIBLE
        AuthRepository.fetchSubscriptionState(pkg.publisherUid, uid) { subscribed ->
            isSubscribed = subscribed
            renderSubscribeState()
        }

        btn.setOnClickListener {
            if (!isOnline()) { showOfflineOverlay(); return@setOnClickListener }

            val previousSubscribed = isSubscribed
            isSubscribed = !isSubscribed
            renderSubscribeState()

            AuthRepository.toggleSubscription(pkg.publisherUid, uid) { subscribed, success ->
                isSubscribed = if (success) subscribed else previousSubscribed
                renderSubscribeState()
            }
        }
    }

    /** يعكس [isSubscribed] الحالية على نص زر الانتساب وخلفيته ولون نصه. */
    private fun renderSubscribeState() {
        val btn = findViewById<TextView>(R.id.btnDetailSubscribe)
        btn.text = getString(
            if (isSubscribed) R.string.action_subscribed_publisher else R.string.action_subscribe_publisher
        )
        btn.setBackgroundResource(
            if (isSubscribed) R.drawable.bg_subscribe_button_active else R.drawable.bg_subscribe_button
        )
        btn.setTextColor(getColor(if (isSubscribed) android.R.color.white else R.color.rin_accent))
    }

    private fun bindReadmeAndLicense() {
        val contents = PackagingUtils.readContents(pkg)

        val txtReadme = findViewById<TextView>(R.id.txtDetailReadme)
        val readme = contents.readme
        if (readme.isNullOrBlank()) {
            txtReadme.text = getString(R.string.package_detail_no_readme)
        } else {
            txtReadme.text = MarkdownLite.toSpannable(readme)
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
            row.findViewById<ImageView>(R.id.imgFileIcon).setImageResource(PackagingUtils.iconResFor(file.name))
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
