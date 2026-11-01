package com.dlof.rinlang.store

import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.TextView
import com.dlof.rinlang.R
import com.dlof.rinlang.auth.AuthRepository
import com.dlof.rinlang.auth.RinUser
import com.dlof.rinlang.network.BaseConnectivityActivity

/**
 * شاشة قائمة مستخدمين عامة: تعرض إمّا "المنتسبين" (متابعو حساب [EXTRA_TARGET_UID]) أو
 * "الانتسابات" (الحسابات التي يتابعها)، حسب [EXTRA_MODE]. كل صف قابل للضغط لفتح
 * [PublicProfileActivity] الخاصة بذلك المستخدم.
 */
class UserListActivity : BaseConnectivityActivity() {

    enum class Mode { SUBSCRIBERS, SUBSCRIPTIONS }

    companion object {
        const val EXTRA_TARGET_UID = "extra_target_uid"
        const val EXTRA_MODE = "extra_mode"

        fun start(context: Context, targetUid: String, mode: Mode) {
            val intent = Intent(context, UserListActivity::class.java)
            intent.putExtra(EXTRA_TARGET_UID, targetUid)
            intent.putExtra(EXTRA_MODE, mode.name)
            context.startActivity(intent)
        }
    }

    private lateinit var targetUid: String
    private lateinit var mode: Mode

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_user_list)

        targetUid = intent.getStringExtra(EXTRA_TARGET_UID) ?: run { finish(); return }
        mode = Mode.valueOf(intent.getStringExtra(EXTRA_MODE) ?: Mode.SUBSCRIBERS.name)

        findViewById<View>(R.id.btnToolbarBack).setOnClickListener { finish() }
        findViewById<TextView>(R.id.txtToolbarTitle).text = getString(
            if (mode == Mode.SUBSCRIBERS) R.string.user_list_title_subscribers else R.string.user_list_title_subscriptions
        )

        loadList()
    }

    override fun onConnectionRestored() {
        loadList()
    }

    private fun loadList() {
        val onLoaded: (List<RinUser>) -> Unit = { users -> renderUsers(users) }
        if (mode == Mode.SUBSCRIBERS) {
            AuthRepository.fetchSubscribers(targetUid, onLoaded)
        } else {
            AuthRepository.fetchSubscriptions(targetUid, onLoaded)
        }
    }

    private fun renderUsers(users: List<RinUser>) {
        val container = findViewById<LinearLayout>(R.id.containerUserList)
        val txtEmpty = findViewById<TextView>(R.id.txtUserListEmpty)
        container.removeAllViews()

        if (users.isEmpty()) {
            txtEmpty.text = getString(
                if (mode == Mode.SUBSCRIBERS) R.string.user_list_empty_subscribers else R.string.user_list_empty_subscriptions
            )
            txtEmpty.visibility = View.VISIBLE
            return
        }
        txtEmpty.visibility = View.GONE

        val inflater = LayoutInflater.from(this)
        for (user in users) {
            val row = inflater.inflate(R.layout.item_user_row, container, false)
            bindUserRow(row, user)
            row.setOnClickListener {
                PublicProfileActivity.start(this, user.uid)
            }
            container.addView(row)
        }
    }

    /** يملأ صف مستخدم واحد: الاسم/اسم المستخدم، شارة التوثيق، وصورة الملف الشخصي (أو حرف بديل). */
    private fun bindUserRow(row: View, user: RinUser) {
        val displayName = user.name.ifBlank { user.username }
        row.findViewById<TextView>(R.id.txtUserRowName).text = displayName
        row.findViewById<TextView>(R.id.txtUserRowUsername).text = "@${user.username}"

        PackageRepository.fetchUserPackages(user.uid) { packages ->
            row.findViewById<ImageView>(R.id.imgUserRowVerifiedBadge).visibility =
                if (PublisherBadgeUtils.isEligible(packages)) View.VISIBLE else View.GONE
        }

        val imgAvatar = row.findViewById<ImageView>(R.id.imgUserRowAvatar)
        val txtInitial = row.findViewById<TextView>(R.id.txtUserRowInitial)
        AvatarUtils.renderAvatar(imgAvatar, txtInitial, user.avatarBase64, displayName)
    }
}
