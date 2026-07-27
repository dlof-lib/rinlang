package com.dlof.rinlang

import android.app.Activity
import android.content.Intent
import android.view.View
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.TextView
import androidx.core.content.ContextCompat
import com.dlof.rinlang.store.AccountActivity
import com.dlof.rinlang.store.RinStoreActivity

/**
 * يربط شريط التنقل السفلي الموحّد (bottom_nav_bar.xml) بالأقسام الرئيسية الأربعة:
 * المحرر (MainActivity) / المشاريع (ProjectsActivity) / المتجر (RinStoreActivity) /
 * حسابي (AccountActivity). يُستدعى مرة واحدة من onCreate في كل شاشة من هذه الشاشات
 * الأربع بعد setContentView، مع تمرير التبويب الحالي لإبرازه بصريًا.
 *
 * كل شاشة من الأربع مُعلَّمة launchMode="singleTop" في AndroidManifest، لذا نستخدم
 * FLAG_ACTIVITY_REORDER_TO_FRONT عند الانتقال بينها بدل إنشاء نسخة جديدة في كل مرة.
 */
enum class BottomNavTab { EDITOR, PROJECTS, STORE, PROFILE, MORE }

object BottomNavHelper {

    fun setup(activity: Activity, current: BottomNavTab) {
        val bar = activity.findViewById<View?>(R.id.bottomNavBar) ?: return

        bindTab(
            activity,
            tabId = R.id.navEditor,
            iconId = R.id.navEditorIcon,
            textId = R.id.navEditorText,
            selected = current == BottomNavTab.EDITOR,
            target = MainActivity::class.java
        )
        bindTab(
            activity,
            tabId = R.id.navProjects,
            iconId = R.id.navProjectsIcon,
            textId = R.id.navProjectsText,
            selected = current == BottomNavTab.PROJECTS,
            target = ProjectsActivity::class.java
        )
        bindTab(
            activity,
            tabId = R.id.navStore,
            iconId = R.id.navStoreIcon,
            textId = R.id.navStoreText,
            selected = current == BottomNavTab.STORE,
            target = RinStoreActivity::class.java
        )
        bindTab(
            activity,
            tabId = R.id.navProfile,
            iconId = R.id.navProfileIcon,
            textId = R.id.navProfileText,
            selected = current == BottomNavTab.PROFILE,
            target = AccountActivity::class.java
        )
        bindTab(
            activity,
            tabId = R.id.navMore,
            iconId = R.id.navMoreIcon,
            textId = R.id.navMoreText,
            selected = current == BottomNavTab.MORE,
            target = MoreActivity::class.java
        )
    }

    private fun bindTab(
        activity: Activity,
        tabId: Int,
        iconId: Int,
        textId: Int,
        selected: Boolean,
        target: Class<*>
    ) {
        val tab = activity.findViewById<LinearLayout?>(tabId) ?: return
        val icon = activity.findViewById<ImageView?>(iconId)
        val text = activity.findViewById<TextView?>(textId)

        val color = ContextCompat.getColor(
            activity,
            if (selected) R.color.rin_bottom_nav_selected else R.color.rin_bottom_nav_unselected
        )
        icon?.setColorFilter(color)
        text?.setTextColor(color)

        tab.setOnClickListener {
            if (selected) return@setOnClickListener
            if (activity::class.java == target) return@setOnClickListener
            val intent = Intent(activity, target).apply {
                flags = Intent.FLAG_ACTIVITY_REORDER_TO_FRONT
            }
            activity.startActivity(intent)
        }
    }
}
