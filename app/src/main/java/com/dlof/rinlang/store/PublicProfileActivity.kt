package com.dlof.rinlang.store

import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.os.Bundle
import android.util.Base64
import android.view.View
import android.widget.ImageView
import android.widget.TextView
import android.widget.Toast
import com.dlof.rinlang.R
import com.dlof.rinlang.auth.AuthRepository
import com.dlof.rinlang.auth.RinUser
import com.dlof.rinlang.network.BaseConnectivityActivity

/**
 * "الملف الشخصي العام": يعرض ملف أي مستخدم في التطبيق (وليس ملفي أنا فقط كما في
 * [AccountActivity]) — صورة + اسم + اسم مستخدم + نبذة + شارة توثيق، عدّاد "المنتسبين" و
 * "الانتسابات" (كل منهما يفتح [UserListActivity])، وزر "الانتساب" (متابعة هذا الحساب).
 *
 * تُفتح من صف الناشر داخل [PackageDetailActivity]، ومن أي صف مستخدم داخل [UserListActivity].
 * زر الانتساب ورقم المنتسبين محدّثان تفاؤلياً بنفس أسلوب bindSubscribe في PackageDetailActivity.
 */
class PublicProfileActivity : BaseConnectivityActivity() {

    companion object {
        const val EXTRA_UID = "extra_uid"

        fun start(context: Context, uid: String) {
            val intent = Intent(context, PublicProfileActivity::class.java)
            intent.putExtra(EXTRA_UID, uid)
            context.startActivity(intent)
        }
    }

    private lateinit var profileUid: String
    private var isSubscribed = false
    private var subscriberCount = 0L

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_public_profile)

        profileUid = intent.getStringExtra(EXTRA_UID) ?: run { finish(); return }

        findViewById<View>(R.id.btnToolbarBack).setOnClickListener { finish() }
        findViewById<TextView>(R.id.txtToolbarTitle).text = getString(R.string.public_profile_title)

        findViewById<View>(R.id.rowProfileSubscribers).setOnClickListener {
            UserListActivity.start(this, profileUid, UserListActivity.Mode.SUBSCRIBERS)
        }
        findViewById<View>(R.id.rowProfileSubscriptions).setOnClickListener {
            UserListActivity.start(this, profileUid, UserListActivity.Mode.SUBSCRIPTIONS)
        }

        refresh()
    }

    override fun onConnectionRestored() {
        refresh()
    }

    private fun refresh() {
        AuthRepository.fetchProfile(profileUid) { profile ->
            bindProfile(profile)
        }
        PackageRepository.fetchUserPackages(profileUid) { packages ->
            findViewById<ImageView>(R.id.imgProfileVerifiedBadge).visibility =
                if (PublisherBadgeUtils.isEligible(packages)) View.VISIBLE else View.GONE
            bindAggregateStats(packages)
        }
        bindSubscribe()
    }

    private fun bindProfile(profile: RinUser?) {
        val displayName = profile?.name?.ifBlank { profile.username } ?: "—"
        findViewById<TextView>(R.id.txtProfileName).text = displayName
        findViewById<TextView>(R.id.txtProfileUsername).text = "@${profile?.username ?: ""}"

        val txtBio = findViewById<TextView>(R.id.txtProfileBio)
        if (profile?.bio.isNullOrBlank()) {
            txtBio.text = getString(R.string.public_profile_bio_empty)
            txtBio.alpha = 0.7f
        } else {
            txtBio.text = profile?.bio
            txtBio.alpha = 1f
        }

        subscriberCount = profile?.subscriberCount ?: 0L
        findViewById<TextView>(R.id.txtProfileSubscriberCount).text = subscriberCount.toString()
        findViewById<TextView>(R.id.txtProfileSubscriptionsCount).text =
            (profile?.subscriptionsCount ?: 0L).toString()

        renderAvatar(profile, displayName)
    }

    /**
     * يحسب ويعرض إحصائيات "الحزم" و"التنزيلات" و"الإعجابات" في صفّ البطاقات الجديد — بيانات
     * حقيقية مُجمَّعة من نفس قائمة [packages] التي وصلت أصلاً من [PackageRepository.fetchUserPackages]
     * (لا طلب شبكة إضافي ولا بيانات وهمية): عدد الحزم = حجم القائمة، والتنزيلات/الإعجابات =
     * مجموع [RinPackage.downloadCount] و[RinPackage.likeCount] عبر كل حزم هذا الناشر.
     */
    private fun bindAggregateStats(packages: List<RinPackage>) {
        findViewById<TextView>(R.id.txtProfilePackagesCount).text = packages.size.toString()
        findViewById<TextView>(R.id.txtProfileDownloadsCount).text =
            packages.sumOf { it.downloadCount }.toString()
        findViewById<TextView>(R.id.txtProfileLikesCount).text =
            packages.sumOf { it.likeCount }.toString()
    }

    /** يعرض صورة الملف الشخصي إن وُجدت (فكّ base64 وعرضها)، وإلا يعرض شارة بحرف الاسم الأول. */
    private fun renderAvatar(profile: RinUser?, displayName: String) {
        val imgAvatar = findViewById<ImageView>(R.id.imgProfileAvatar)
        val txtInitial = findViewById<TextView>(R.id.txtProfileAvatarInitial)

        clipToCircle(imgAvatar)

        val avatarBase64 = profile?.avatarBase64
        if (avatarBase64.isNullOrBlank()) {
            imgAvatar.setImageDrawable(null)
            txtInitial.text = displayName.take(1).uppercase()
            txtInitial.visibility = View.VISIBLE
        } else {
            try {
                val bytes = Base64.decode(avatarBase64, Base64.NO_WRAP)
                val bitmap = BitmapFactory.decodeByteArray(bytes, 0, bytes.size)
                if (bitmap != null) {
                    imgAvatar.setImageBitmap(bitmap)
                    txtInitial.visibility = View.GONE
                } else {
                    txtInitial.text = displayName.take(1).uppercase()
                    txtInitial.visibility = View.VISIBLE
                }
            } catch (t: Throwable) {
                txtInitial.text = displayName.take(1).uppercase()
                txtInitial.visibility = View.VISIBLE
            }
        }
    }

    private fun clipToCircle(view: ImageView) {
        view.clipToOutline = true
        view.outlineProvider = object : android.view.ViewOutlineProvider() {
            override fun getOutline(v: View, outline: android.graphics.Outline) {
                outline.setOval(0, 0, v.width, v.height)
            }
        }
    }

    /**
     * يهيّئ زر "الانتساب" لهذا الحساب: يبقى مخفياً إن لم يكن هناك مستخدم مسجَّل دخوله بعد أو
     * كان المستخدم الحالي هو صاحب هذا الملف نفسه (لا معنى للانتساب للنفس)، ثم يجلب حالة
     * الانتساب الحالية ليضبط نص الزر (انتساب / تم الانتساب). الضغط يبدّل الحالة والعدّاد
     * محلياً فوراً (تفاؤلي) ثم يُزامنها مع Firebase، ويتراجع عن التغيير المحلي إن فشلت المزامنة.
     */
    private fun bindSubscribe() {
        val btn = findViewById<TextView>(R.id.btnProfileSubscribe)
        val uid = AuthRepository.currentUid()

        if (uid == null || uid == profileUid) {
            btn.visibility = View.GONE
            return
        }

        btn.visibility = View.VISIBLE
        AuthRepository.fetchSubscriptionState(profileUid, uid) { subscribed ->
            isSubscribed = subscribed
            renderSubscribeState()
        }

        btn.setOnClickListener {
            if (!isOnline()) { showOfflineOverlay(); return@setOnClickListener }

            val previousSubscribed = isSubscribed
            val previousCount = subscriberCount
            isSubscribed = !isSubscribed
            subscriberCount = if (isSubscribed) subscriberCount + 1 else (subscriberCount - 1).coerceAtLeast(0L)
            renderSubscribeState()

            AuthRepository.toggleSubscription(profileUid, uid) { subscribed, success ->
                if (success) {
                    isSubscribed = subscribed
                } else {
                    isSubscribed = previousSubscribed
                    subscriberCount = previousCount
                    Toast.makeText(this, R.string.subscribe_action_failed, Toast.LENGTH_SHORT).show()
                }
                renderSubscribeState()
            }
        }
    }

    /** يعكس [isSubscribed] و[subscriberCount] الحاليَين على نص/خلفية زر الانتساب وعدّاد المنتسبين. */
    private fun renderSubscribeState() {
        val btn = findViewById<TextView>(R.id.btnProfileSubscribe)
        btn.text = getString(
            if (isSubscribed) R.string.action_subscribed_publisher else R.string.action_subscribe_publisher
        )
        btn.setBackgroundResource(
            if (isSubscribed) R.drawable.bg_subscribe_button_active else R.drawable.bg_subscribe_button
        )
        btn.setTextColor(getColor(if (isSubscribed) android.R.color.white else R.color.rin_accent))
        findViewById<TextView>(R.id.txtProfileSubscriberCount).text = subscriberCount.toString()
    }
}
