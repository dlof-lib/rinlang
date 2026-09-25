package com.dlof.rinlang.store

import android.graphics.BitmapFactory
import android.os.Bundle
import android.util.Base64
import android.view.LayoutInflater
import android.view.View
import android.widget.FrameLayout
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import com.dlof.rinlang.IndsinFabricView
import com.dlof.rinlang.R
import com.dlof.rinlang.RinEngine
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

    /** جذر شجرة ملفات الحزمة الحالية (يُبنى مرة واحدة في [bindFiles])، وموقع التصفّح الحالي
     *  داخلها — انظر [renderCurrentFilesFolder]/[renderFilesBreadcrumb] لسلوك تصفّح GitHub. */
    private lateinit var fileTreeRoot: FileTreeFolder
    private var currentFilesPath: List<String> = emptyList()

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

        // احترازي: هذه الشاشة قد تُفتح مباشرة من تبويب المتجر بلا مرور سابق بـMainActivity في
        // نفس دورة حياة العملية، فيبقى RinEngine.baseDir فارغاً — ما يهمّ فقط لمقاطع كود Rin
        // الحيّة في README التي تستخدم save/installation/file (نادر في كود README توضيحي).
        // الشرط هنا مقصود: لا نستدعي init() إن كان baseDir مضبوطاً أصلاً (مثلاً MainActivity ربطه
        // بمشروع حقيقي محدد ولا يزال في الخلفية)، لتفادي إفساد ذلك المسار لبقية جلسة التطبيق —
        // نملأ الفراغ فقط، لا نستبدل قيمة موجودة.
        if (RinEngine.currentBaseDir().isBlank()) {
            RinEngine.init(applicationContext)
        }

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

        // أيقونة الحزمة: تُعرَض صورة الأيقونة الحقيقية إن رفعها الناشر عند النشر (pkg.iconBase64)،
        // وإلا يبقى الحرف الأول من اسم الحزمة كبديل (بنفس الشارة المتدرّجة السابقة).
        val txtIcon = findViewById<TextView>(R.id.txtDetailPackageIcon)
        val imgIcon = findViewById<ImageView>(R.id.imgDetailPackageIcon)
        txtIcon.text = pkg.name.take(1).uppercase()
        val iconBitmap = AvatarUtils.decodeCircularAvatar(pkg.iconBase64)
        if (iconBitmap != null) {
            imgIcon.setImageBitmap(iconBitmap)
            imgIcon.visibility = View.VISIBLE
            txtIcon.visibility = View.INVISIBLE
        } else {
            imgIcon.visibility = View.GONE
            txtIcon.visibility = View.VISIBLE
        }

        // الصورة المصغّرة (thumbnail): تحلّ محل بانر الخلايا السداسية الافتراضي في رأس الصفحة
        // إن رفعها الناشر، لتمييز كل حزمة بصرياً عن غيرها.
        if (!pkg.thumbnailBase64.isBlank()) {
            try {
                val bytes = Base64.decode(pkg.thumbnailBase64, Base64.NO_WRAP)
                val thumb = BitmapFactory.decodeByteArray(bytes, 0, bytes.size)
                if (thumb != null) {
                    findViewById<ImageView>(R.id.imgDetailHeroBanner).setImageBitmap(thumb)
                }
            } catch (t: Throwable) {
                // يبقى البانر الافتراضي إن تعذّر فكّ الصورة المصغّرة
            }
        }

        val txtDescription = findViewById<TextView>(R.id.txtDetailDescription)
        txtDescription.text = pkg.description
        txtDescription.visibility = if (pkg.description.isBlank()) View.GONE else View.VISIBLE

        // صفّ الإحصائيات المقسَّم إلى بطاقات مفردة (بدل نص واحد مدموج package_detail_meta_format
        // سابقاً) — نفس القيم بالضبط (pkg.version / pkg.downloadCount / pkg.averageRating و
        // pkg.ratingCount)، فقط معروضة في ثلاث حبّات منفصلة لتحسين وضوح القراءة. الترخيص
        // (pkg.license) بقي معروضاً فقط في قسمه المخصَّص أسفل الصفحة (txtDetailLicense).
        findViewById<TextView>(R.id.txtDetailStatVersion).text =
            getString(R.string.package_detail_stat_version_format, pkg.version)
        findViewById<TextView>(R.id.txtDetailStatDownloads).text =
            getString(R.string.package_detail_stat_downloads_format, pkg.downloadCount)
        findViewById<TextView>(R.id.txtDetailStatRating).text =
            getString(R.string.package_detail_stat_rating_format, pkg.averageRating, pkg.ratingCount)
    }

    private fun bindPublisher() {
        val txtName = findViewById<TextView>(R.id.txtDetailPublisherName)
        val txtInitial = findViewById<TextView>(R.id.txtDetailPublisherInitial)
        val imgAvatar = findViewById<ImageView>(R.id.imgDetailPublisherAvatar)
        val imgBadge = findViewById<ImageView>(R.id.imgDetailVerifiedBadge)

        findViewById<View>(R.id.rowDetailPublisher).setOnClickListener {
            PublicProfileActivity.start(this, pkg.publisherUid)
        }

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

        // القصّ الدائري يتم على مستوى الـ Bitmap نفسه عبر AvatarUtils.renderAvatar، لضمان ظهور
        // صورة الناشر كاملة ودائرية دائماً بلا أي قطع جزئي.
        AuthRepository.fetchProfile(pkg.publisherUid) { profile ->
            AvatarUtils.renderAvatar(imgAvatar, txtInitial, profile?.avatarBase64, pkg.publisherName)
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

    /**
     * يعرض قسم README.md كسلسلة مقاطع (عبر [MarkdownLite.splitLiveCodeBlocks]) بدل TextView
     * واحد ثابت: كل مقطع نصي عادي يُعرَض كالمعتاد، وكل كتلة كود ```rin ```/```indsin ``` تُستخرَج
     * إلى بطاقة "معاينة حية" منفصلة (عبر [buildLivePreviewCard]) — تشغيل حقيقي للكود عبر المحرّك
     * الأصلي، لا مجرّد نص كود ثابت.
     */
    private fun bindReadmeAndLicense() {
        val contents = PackagingUtils.readContents(pkg)

        val containerReadme = findViewById<LinearLayout>(R.id.containerDetailReadme)
        containerReadme.removeAllViews()
        val readme = contents.readme
        if (readme.isNullOrBlank()) {
            containerReadme.addView(buildReadmeTextSegment(getString(R.string.package_detail_no_readme)))
        } else {
            for (segment in MarkdownLite.splitLiveCodeBlocks(readme)) {
                when (segment) {
                    is MarkdownLite.MarkdownSegment.Text -> {
                        if (segment.markdown.isNotBlank()) {
                            containerReadme.addView(buildReadmeTextSegment(segment.markdown))
                        }
                    }
                    is MarkdownLite.MarkdownSegment.LiveCode -> {
                        containerReadme.addView(buildLivePreviewCard(segment.code))
                    }
                }
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

    /** مقطع نصّ Markdown عادي واحد داخل قسم README (انظر [bindReadmeAndLicense]) — بنفس تنسيق
     *  الـTextView الوحيد السابق تماماً (حجم/تباعد سطر/لون/روابط قابلة للنقر). */
    private fun buildReadmeTextSegment(markdown: String): TextView = TextView(this).apply {
        layoutParams = LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT
            // فراغ أكبر قليلاً بين مقاطع README (كان 4dp) — يفصل الفقرات/الكتل بصرياً بوضوح
            // أكبر، بنفس روح تباعد فقرات GitHub README، بدل أن تبدو مقاطع متلاصقة.
        ).apply { if (containerReadmeHasContent) topMargin = dp(10f) }
        // حجم أكبر قليلاً (كان 13sp) وتباعد سطر أوسع (كان 3dp/×1) لراحة قراءة أطول بلا إجهاد
        // للعين، خصوصاً في النصوص الطويلة — أقرب لتجربة قراءة مستندات فعلية من نص مضغوط.
        textSize = 14.5f
        setLineSpacing(dp(6f).toFloat(), 1.12f)
        letterSpacing = 0.005f
        setTextColor(getColor(R.color.rin_editor_text))
        autoLinkMask = android.text.util.Linkify.WEB_URLS
        MarkdownLite.applyTo(this, markdown)
    }

    /** يبقى false فقط قبل أول مقطع يُضاف فعلياً — يُستخدَم فقط لتفادي هامش علوي زائد لأول مقطع. */
    private val containerReadmeHasContent: Boolean
        get() = findViewById<LinearLayout>(R.id.containerDetailReadme).childCount > 0

    /**
     * يبني بطاقة "معاينة حية" واحدة لكتلة كود Rin/indsin مستخرَجة من README: كود المقطع نفسه
     * (بنفس تنسيق كتلة الكود القياسي في [MarkdownLite])، ثم شارة حالة + إطار معاينة يُملأ فعلياً
     * عبر [renderLivePreviewFrame] — لا شيء هنا مُحاكى أو ثابت مسبقاً.
     */
    private fun buildLivePreviewCard(code: String): View {
        val card = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT
            ).apply { if (containerReadmeHasContent) topMargin = dp(4f) }
        }

        val txtCode = TextView(this).apply {
            layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT
            )
            textSize = 13.5f
            setLineSpacing(dp(5f).toFloat(), 1.08f)
        }
        // نستخدم applyTo بدل تعيين .text مباشرة: يُفعِّل LinkMovementMethod، وهو ما يجعل زر
        // "نسخ" الجديد أعلى بطاقة الكود (MarkdownLite.CopyCodeSpan) قابلاً للنقر فعلياً هنا أيضاً،
        // لا فقط داخل مقاطع README النصية العادية (buildReadmeTextSegment).
        MarkdownLite.applyTo(txtCode, "```rin\n$code\n```")
        card.addView(txtCode)

        val badge = TextView(this).apply {
            layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT
            ).apply { topMargin = dp(10f) }
            setBackgroundResource(R.drawable.bg_live_preview_badge)
            setPadding(dp(10f), dp(4f), dp(10f), dp(4f))
            textSize = 10.5f
            setTextColor(getColor(R.color.rin_accent))
            setTypeface(typeface, android.graphics.Typeface.BOLD)
            text = getString(R.string.package_detail_live_preview_label)
        }
        card.addView(badge)

        val frame = FrameLayout(this).apply {
            layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT
            ).apply { topMargin = dp(8f) }
            setBackgroundResource(R.drawable.bg_live_preview_frame)
            clipToOutline = true
        }
        card.addView(frame)

        renderLivePreviewFrame(frame, badge, code)
        return card
    }

    /**
     * يحاول تشغيل [code] فعلياً كوجهة/صفحة Loom عبر [RinEngine.renderView]
     * (نفس مسار المعاينة الحيّة الحقيقي في المحرّر) ويرسمها كمعاينة حقيقية داخل [frame] عبر
     * [com.dlof.rinlang.IndsinFabricView] عند وجود `@view` قابل للعرض. إن لم يوجد (حاوية
     * `@container` بلا واجهة، كود جدولة/منطق عادي...) ينزل إلى [renderExecutionOutput] لعرض
     * نتيجة تنفيذ حقيقية بدل إطار فارغ لا يعني شيئاً. أي فشل (خطأ لغوي، استثناء JNI...) يُعرَض
     * كنص خطأ واضح داخل الإطار نفسه بدل ترك المستخدم أمام معاينة صامتة.
     */
    private fun renderLivePreviewFrame(frame: FrameLayout, badge: TextView, code: String) {
        val previewWidthPx = dp(280f)
        try {
            val result = org.json.JSONObject(RinEngine.renderView(code, previewWidthPx))
            val fabric = result.optJSONObject("fabric")
            if (fabric != null) {
                badge.text = getString(R.string.package_detail_live_preview_label)
                val h = fabric.optDouble("h", 480.0).toInt().coerceIn(120, 900)
                val fabricView = IndsinFabricView(this)
                frame.addView(
                    fabricView,
                    FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, dp(h.toFloat()))
                )
                fabricView.setFabric(fabric, previewWidthPx, h, result.optJSONArray("overlays"))
            } else {
                renderExecutionOutput(frame, badge, code)
            }
        } catch (t: Throwable) {
            badge.text = getString(R.string.package_detail_live_preview_error_label)
            frame.addView(buildLivePreviewOutputText(t.message ?: t.toString(), isError = true))
        }
    }

    /**
     * ينزل إليه [renderLivePreviewFrame] عندما لا تحوي كتلة الكود `@view` قابلاً للعرض — يُشغِّل
     * [code] فعلياً عبر مسار التنفيذ الهيكلي [RinEngine.runSourceStructured]
     * (نفس محرّك التشغيل الحقيقي، وينجح مع أي كود Rin صحيح: حاوية `@container`، جدولة، منطق عادي)
     * ويعرض ناتجه المطبوع الحقيقي، أو رسالة الخطأ الحقيقية إن فشل التنفيذ.
     */
    private fun renderExecutionOutput(frame: FrameLayout, badge: TextView, code: String) {
        try {
            val result = RinEngine.runSourceStructured(code)
            if (result.success) {
                badge.text = getString(R.string.package_detail_live_preview_output_label)
                val text = result.output.ifBlank { getString(R.string.package_detail_live_preview_no_output) }
                frame.addView(buildLivePreviewOutputText(text, isError = false))
            } else {
                badge.text = getString(R.string.package_detail_live_preview_error_label)
                val message = result.errorMessage ?: result.diagnosticText ?: getString(R.string.indsin_unknown_error)
                val text = if (result.errorLine > 0) {
                    getString(R.string.indsin_error_line_format, result.errorLine, message)
                } else {
                    getString(R.string.indsin_error_format, message)
                }
                frame.addView(buildLivePreviewOutputText(text, isError = true))
            }
        } catch (t: Throwable) {
            badge.text = getString(R.string.package_detail_live_preview_error_label)
            frame.addView(buildLivePreviewOutputText(t.message ?: t.toString(), isError = true))
        }
    }

    private fun buildLivePreviewOutputText(text: String, isError: Boolean): TextView = TextView(this).apply {
        layoutParams = FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.WRAP_CONTENT
        )
        setPadding(dp(12f), dp(12f), dp(12f), dp(12f))
        typeface = android.graphics.Typeface.MONOSPACE
        textSize = 12f
        setTextColor(getColor(if (isError) R.color.status_error else R.color.rin_editor_text))
        this.text = text
    }

    /** يحوّل [value] (dp) إلى بكسل فعلي حسب كثافة الشاشة الحالية — تُستخدَم في كل القياسات
     *  المبنية بالكود لبطاقات README/ملفات الحزمة أعلاه. */
    private fun dp(value: Float): Int = (value * resources.displayMetrics.density).toInt()

    /**
     * يعرض قسم "ملفات الحزمة" بتصفّح شبيه بمستكشف ملفات GitHub الحقيقي: مستوى واحد من
     * المجلدات/الملفات في كل مرة (لا شجرة مفتوحة بالكامل بإزاحة متدرّجة كما كان سابقاً) — لمس
     * صفّ مجلد "يدخل" إليه فعلياً ويستبدل القائمة بمحتواه، وشريط المسار (breadcrumb) أعلى القائمة
     * (عبر [renderFilesBreadcrumb]) يبقى يعرض المسار الحالي بأكمله وكل جزء منه قابل للمس للعودة
     * إليه مباشرة — تماماً كسلوك "food/img/example.png" القابل للنقر في أعلى صفحة ملفات GitHub.
     */
    private fun bindFiles() {
        val contents = PackagingUtils.readContents(pkg)
        fileTreeRoot = PackagingUtils.buildFileTree(contents.files)
        currentFilesPath = emptyList()
        renderCurrentFilesFolder()
        renderFilesLanguageBar(contents.files)
    }

    /**
     * يعرض شريط "تركيبة ملفات الحزمة" أسفل القائمة (انظر [PackagingUtils.languageBreakdown]) —
     * قطعة ملوَّنة بعرض نسبي لكل نوع ملف فوق شريط رفيع مدوَّر الحواف، وصف حبّات (chips) تحته
     * (نقطة ملوَّنة + تسمية + نسبة%) داخل تمرير أفقي. يُخفي القسم كاملاً (الشريط + الفاصل) عند
     * وجود نوع ملف واحد أو أقل (لا فائدة بصرية من شريط بلون واحد).
     */
    private fun renderFilesLanguageBar(files: List<PackageFileEntry>) {
        val footer = findViewById<LinearLayout>(R.id.containerFilesLanguageBar)
        val divider = findViewById<View>(R.id.dividerFilesLanguageBar)
        footer.removeAllViews()

        val slices = PackagingUtils.languageBreakdown(files)
        if (slices.size < 2) {
            footer.visibility = View.GONE
            divider.visibility = View.GONE
            return
        }
        footer.visibility = View.VISIBLE
        divider.visibility = View.VISIBLE

        val trackFrame = FrameLayout(this).apply {
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, dp(6f))
            setBackgroundResource(R.drawable.bg_language_bar_track)
            clipToOutline = true
        }
        val segmentsRow = LinearLayout(this).apply {
            layoutParams = FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT)
            orientation = LinearLayout.HORIZONTAL
        }
        for (slice in slices) {
            val segment = View(this).apply {
                layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, slice.percent.coerceAtLeast(0.5f))
                setBackgroundColor(getColor(slice.colorRes))
            }
            segmentsRow.addView(segment)
        }
        trackFrame.addView(segmentsRow)
        footer.addView(trackFrame)

        val legendScroll = android.widget.HorizontalScrollView(this).apply {
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply {
                topMargin = dp(10f)
            }
            isHorizontalScrollBarEnabled = false
        }
        val legendRow = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = android.view.Gravity.CENTER_VERTICAL
        }
        for ((index, slice) in slices.withIndex()) {
            val chip = LinearLayout(this).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = android.view.Gravity.CENTER_VERTICAL
                layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply {
                    if (index > 0) marginStart = dp(14f)
                }
            }
            val dot = View(this).apply {
                layoutParams = LinearLayout.LayoutParams(dp(8f), dp(8f))
                background = android.graphics.drawable.GradientDrawable().apply {
                    shape = android.graphics.drawable.GradientDrawable.OVAL
                    setColor(getColor(slice.colorRes))
                }
            }
            chip.addView(dot)
            val label = TextView(this).apply {
                layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply {
                    marginStart = dp(6f)
                }
                textSize = 11f
                setTextColor(getColor(R.color.rin_editor_hint))
                text = getString(R.string.package_detail_language_chip_format, slice.label, slice.percent)
            }
            chip.addView(label)
            legendRow.addView(chip)
        }
        legendScroll.addView(legendRow)
        footer.addView(legendScroll)
    }

    /** المجلد المطابق لـ[currentFilesPath] الحالي داخل [fileTreeRoot] (الجذر نفسه إن كان المسار فارغاً). */
    private fun currentFilesFolder(): FileTreeFolder {
        var node = fileTreeRoot
        for (segment in currentFilesPath) {
            node = node.folders[segment] ?: return node
        }
        return node
    }

    /** يعيد رسم شريط المسار وقائمة المستوى الحالي معاً — نقطة الدخول الوحيدة بعد أي تنقّل. */
    private fun renderCurrentFilesFolder() {
        renderFilesBreadcrumb()

        val container = findViewById<LinearLayout>(R.id.containerDetailFiles)
        container.removeAllViews()
        val folder = currentFilesFolder()
        val inflater = LayoutInflater.from(this)
        var rowIndex = 0

        for (subFolder in folder.folders.values) {
            val row = inflater.inflate(R.layout.item_package_folder, container, false)
            row.findViewById<TextView>(R.id.txtFolderName).text = subFolder.name
            row.findViewById<TextView>(R.id.txtFolderCount).text =
                getString(R.string.package_detail_folder_file_count, subFolder.totalFileCount())
            if (rowIndex % 2 == 1) row.setBackgroundColor(withAlpha(getColor(R.color.rin_on_toolbar), 8))
            rowIndex++
            row.setOnClickListener {
                currentFilesPath = currentFilesPath + subFolder.name
                renderCurrentFilesFolder()
            }
            container.addView(row)
        }

        for (leaf in folder.files) {
            val row = inflater.inflate(R.layout.item_package_file, container, false)
            val tint = getColor(PackagingUtils.iconColorResFor(leaf.entry.name))
            row.findViewById<ImageView>(R.id.imgFileIcon).apply {
                setImageResource(PackagingUtils.iconResFor(leaf.entry.name))
                imageTintList = android.content.res.ColorStateList.valueOf(tint)
            }
            row.findViewById<View>(R.id.bgFileIconCircle).backgroundTintList =
                android.content.res.ColorStateList.valueOf(withAlpha(tint, 38))
            row.findViewById<TextView>(R.id.txtFileName).text = leaf.simpleName
            row.findViewById<TextView>(R.id.txtFileSize).text = formatSize(leaf.entry.sizeBytes)
            // تباين خفيف بين الصفوف الزوجية/الفردية بدل خلفية واحدة موحّدة مسطّحة، لتحسين قابلية
            // المسح البصري (scannability) — العدّاد مشترك بين المجلدات والملفات معاً حتى يبقى
            // التناوب متّسقاً عبر المستوى الحالي كاملاً لا داخل كل نوع وحده.
            if (rowIndex % 2 == 1) row.setBackgroundColor(withAlpha(getColor(R.color.rin_on_toolbar), 8))
            rowIndex++
            container.addView(row)
        }
    }

    /**
     * يبني شريط المسار (breadcrumb) أعلى قائمة الملفات: اسم الحزمة كجذر قابل للمس للعودة إليه
     * مباشرة، ثم كل جزء من [currentFilesPath] كصفّ منفصل قابل للمس بدوره للقفز لذلك المستوى
     * تحديداً — آخر جزء (المستوى الحالي) بلون مختلف وغير قابل للمس لأنه هو المعروض أصلاً.
     */
    private fun renderFilesBreadcrumb() {
        val container = findViewById<LinearLayout>(R.id.containerFilesBreadcrumb)
        container.removeAllViews()

        addBreadcrumbCrumb(container, pkg.name, isCurrent = currentFilesPath.isEmpty()) {
            currentFilesPath = emptyList()
            renderCurrentFilesFolder()
        }

        for (i in currentFilesPath.indices) {
            addBreadcrumbSeparator(container)
            val pathUpToHere = currentFilesPath.subList(0, i + 1).toList()
            addBreadcrumbCrumb(container, currentFilesPath[i], isCurrent = i == currentFilesPath.lastIndex) {
                currentFilesPath = pathUpToHere
                renderCurrentFilesFolder()
            }
        }
    }

    private fun addBreadcrumbCrumb(container: LinearLayout, label: String, isCurrent: Boolean, onCrumbTap: () -> Unit) {
        val crumb = TextView(this).apply {
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT)
            text = label
            textSize = 12.5f
            typeface = android.graphics.Typeface.MONOSPACE
            if (isCurrent) {
                setTextColor(getColor(R.color.rin_on_toolbar))
                setTypeface(typeface, android.graphics.Typeface.BOLD)
            } else {
                setTextColor(getColor(R.color.rin_accent))
                setPadding(dp(4f), dp(4f), dp(4f), dp(4f))
                isClickable = true
                isFocusable = true
                setOnClickListener { onCrumbTap() }
            }
        }
        container.addView(crumb)
    }

    private fun addBreadcrumbSeparator(container: LinearLayout) {
        val sep = TextView(this).apply {
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply {
                marginStart = dp(2f); marginEnd = dp(2f)
            }
            text = "/"
            textSize = 12.5f
            setTextColor(getColor(R.color.rin_editor_hint))
        }
        container.addView(sep)
    }

    /** يُرجع [color] بنفس قيمة الشفافية [alpha] (0-255) بدل ألفا اللون الأصلية، لخلفيات خفيفة متّسقة. */
    private fun withAlpha(color: Int, alpha: Int): Int =
        android.graphics.Color.argb(
            alpha,
            android.graphics.Color.red(color),
            android.graphics.Color.green(color),
            android.graphics.Color.blue(color)
        )

    private fun formatSize(bytes: Long): String = when {
        bytes < 1024 -> "$bytes B"
        bytes < 1024 * 1024 -> "${bytes / 1024} KB"
        else -> String.format("%.1f MB", bytes / (1024.0 * 1024.0))
    }
}
