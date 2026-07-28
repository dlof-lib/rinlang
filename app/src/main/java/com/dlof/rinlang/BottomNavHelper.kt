package com.dlof.rinlang

import android.animation.ValueAnimator
import android.app.Activity
import android.content.Intent
import android.view.View
import android.view.animation.OvershootInterpolator
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
 *
 * إعادة التصميم: بالإضافة إلى تلوين الأيقونة/النص، يضبط [setup] أيضاً موقع وعرض الحبّة
 * العائمة (bottomNavIndicator) خلف التبويب المُحدَّد بعد أول تخطيط للشريط (عرض كل تبويب
 * weight="1" غير معروف قبل القياس)، بحركة دخول ناعمة (fade + scale) عبر OvershootInterpolator
 * لإحساس "زنبركي" (spring) خفيف بدل ظهور مفاجئ.
 */
enum class BottomNavTab { EDITOR, PROJECTS, STORE, PROFILE, MORE }

object BottomNavHelper {

    fun setup(activity: Activity, current: BottomNavTab) {
        val bar = activity.findViewById<View?>(R.id.bottomNavBar) ?: return
        val indicator = activity.findViewById<View?>(R.id.bottomNavIndicator)

        var selectedTab: View? = null

        selectedTab = bindTab(
            activity,
            tabId = R.id.navEditor,
            iconId = R.id.navEditorIcon,
            textId = R.id.navEditorText,
            selected = current == BottomNavTab.EDITOR,
            target = MainActivity::class.java,
            indicator = indicator
        ) ?: selectedTab

        selectedTab = bindTab(
            activity,
            tabId = R.id.navProjects,
            iconId = R.id.navProjectsIcon,
            textId = R.id.navProjectsText,
            selected = current == BottomNavTab.PROJECTS,
            target = ProjectsActivity::class.java,
            indicator = indicator
        ) ?: selectedTab

        selectedTab = bindTab(
            activity,
            tabId = R.id.navStore,
            iconId = R.id.navStoreIcon,
            textId = R.id.navStoreText,
            selected = current == BottomNavTab.STORE,
            target = RinStoreActivity::class.java,
            indicator = indicator
        ) ?: selectedTab

        selectedTab = bindTab(
            activity,
            tabId = R.id.navProfile,
            iconId = R.id.navProfileIcon,
            textId = R.id.navProfileText,
            selected = current == BottomNavTab.PROFILE,
            target = AccountActivity::class.java,
            indicator = indicator
        ) ?: selectedTab

        selectedTab = bindTab(
            activity,
            tabId = R.id.navMore,
            iconId = R.id.navMoreIcon,
            textId = R.id.navMoreText,
            selected = current == BottomNavTab.MORE,
            target = MoreActivity::class.java,
            indicator = indicator
        ) ?: selectedTab

        // ننتظر أول تخطيط فعلي للشريط (العروض الحقيقية للتبويبات غير معروفة قبل القياس)
        // ثم نضع الحبّة فوراً في موقعها الصحيح مع دخول ناعم (fade + scale spring-like).
        val finalSelected = selectedTab
        if (indicator != null && finalSelected != null) {
            bar.viewTreeObserver.addOnGlobalLayoutListener(object : android.view.ViewTreeObserver.OnGlobalLayoutListener {
                override fun onGlobalLayout() {
                    bar.viewTreeObserver.removeOnGlobalLayoutListener(this)
                    placeIndicatorInstantly(indicator, finalSelected)
                    animateIndicatorEntrance(indicator)
                }
            })
        }
    }

    /** يضع الحبّة فوراً (بلا حركة) في مركز [tab] المُحدَّد، بعرض أصغر من التبويب بهامش 16dp إجمالاً. */
    private fun placeIndicatorInstantly(indicator: View, tab: View) {
        val margin = (8 * indicator.resources.displayMetrics.density).toInt()
        val targetWidth = (tab.width - margin * 2).coerceAtLeast(0)
        indicator.layoutParams = indicator.layoutParams.apply { width = targetWidth }
        indicator.translationX = (tab.left + margin).toFloat()
        indicator.alpha = 0f
        indicator.scaleX = 0.6f
    }

    /** حركة دخول خفيفة "زنبركية" (spring) للحبّة عند فتح الشاشة، بدل ظهورها فجأة. */
    private fun animateIndicatorEntrance(indicator: View) {
        indicator.animate()
            .alpha(1f)
            .scaleX(1f)
            .setDuration(260)
            .setInterpolator(OvershootInterpolator(1.6f))
            .start()
    }

    /**
     * يحرّك الحبّة أفقياً (translationX) من موقعها الحالي إلى تبويب [target] بأسلوب "زنبركي"
     * ناعم عبر ValueAnimator + OvershootInterpolator، قبل مغادرة الشاشة الحالية — لمحة انتقال
     * بصرية سريعة (~180ms) تسبق فتح الشاشة الجديدة (التي ستُعيد رسم شريطها من الصفر).
     */
    private fun animateIndicatorTo(indicator: View, target: View) {
        val margin = (8 * indicator.resources.displayMetrics.density).toInt()
        val targetWidth = (target.width - margin * 2).coerceAtLeast(0)
        val targetX = (target.left + margin).toFloat()

        val startX = indicator.translationX
        val startWidth = indicator.layoutParams.width

        ValueAnimator.ofFloat(0f, 1f).apply {
            duration = 180
            interpolator = OvershootInterpolator(1.2f)
            addUpdateListener { anim ->
                val fraction = anim.animatedFraction
                indicator.translationX = startX + (targetX - startX) * fraction
                indicator.layoutParams = indicator.layoutParams.apply {
                    width = (startWidth + (targetWidth - startWidth) * fraction).toInt()
                }
            }
            start()
        }
    }

    /** يربط تبويباً واحداً بلونه ونقر الانتقال؛ يُعيد View التبويب نفسه إن كان هو المُحدَّد حالياً. */
    private fun bindTab(
        activity: Activity,
        tabId: Int,
        iconId: Int,
        textId: Int,
        selected: Boolean,
        target: Class<*>,
        indicator: View?
    ): View? {
        val tab = activity.findViewById<LinearLayout?>(tabId) ?: return null
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
            if (indicator != null) {
                animateIndicatorTo(indicator, tab)
            }
            val intent = Intent(activity, target).apply {
                flags = Intent.FLAG_ACTIVITY_REORDER_TO_FRONT
            }
            activity.startActivity(intent)
        }

        return if (selected) tab else null
    }
}
