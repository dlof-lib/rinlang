package com.dlof.rinlang.permissions

import android.Manifest
import android.app.Activity
import android.app.AlertDialog
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Typeface
import android.net.Uri
import android.provider.Settings
import android.view.Gravity
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import androidx.core.app.NotificationManagerCompat
import androidx.core.content.ContextCompat
import com.dlof.rinlang.R

/**
 * إدارة الأذونات التي يحتاجها RinStudio فعلاً، مع شرح سبب كل إذن وطلبه في سياقه (لا عند الفتح).
 *
 *  • الإشعارات (POST_NOTIFICATIONS) : إشعار اكتمال التنزيل + خدمة المعاينة الحية. اختياري.
 *  • تثبيت التطبيقات (REQUEST_INSTALL_PACKAGES): زر «تثبيت» لحزم APK المُصدَّرة. اختياري.
 *  • التخزين: لا يحتاج إذناً — التنزيلات تُكتب في Downloads عبر MediaStore (أندرويد 10+)،
 *    والرفع يتم عبر منتقي الملفات/الوسائط الخاص بالنظام. لذلك لا نطلب READ/WRITE_EXTERNAL_STORAGE
 *    ولا MANAGE_EXTERNAL_STORAGE ولا READ_MEDIA_* عمداً.
 *  • الكاميرا: تُلتقط الصور عبر تطبيق الكاميرا نفسه (ACTION_IMAGE_CAPTURE) فلا يلزم إذن CAMERA.
 */
object RinPermissions {

    private const val PREFS = "rin_permissions"
    private const val KEY_ASKED_NOTIF = "asked_notifications"
    private const val REQ_NOTIFICATIONS = 5758

    // ---------------------------------------------------------------- الحالة

    fun notificationsGranted(ctx: Context): Boolean =
        ContextCompat.checkSelfPermission(ctx, Manifest.permission.POST_NOTIFICATIONS) ==
            PackageManager.PERMISSION_GRANTED && NotificationManagerCompat.from(ctx).areNotificationsEnabled()

    fun canInstallApks(ctx: Context): Boolean = ctx.packageManager.canRequestPackageInstalls()

    // ---------------------------------------------------------------- الطلب

    /**
     * يطلب إذن الإشعارات مرة واحدة فقط بعد شرح سببه، ولا يُزعج المستخدم إن رفض.
     * يُستدعى بعد أول تنزيل ناجح (لحظة تظهر فيها فائدة الإشعار).
     */
    fun askNotificationsOnce(activity: Activity) {
        if (notificationsGranted(activity)) return
        val sp = activity.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        if (sp.getBoolean(KEY_ASKED_NOTIF, false)) return
        sp.edit().putBoolean(KEY_ASKED_NOTIF, true).apply()
        AlertDialog.Builder(activity)
            .setTitle(R.string.perm_rationale_title)
            .setMessage(R.string.perm_rationale_msg)
            .setPositiveButton(R.string.perm_allow) { _, _ -> requestNotifications(activity) }
            .setNegativeButton(R.string.perm_not_now, null)
            .show()
    }

    private fun requestNotifications(activity: Activity) {
        runCatching {
            if (activity.shouldShowRequestPermissionRationale(Manifest.permission.POST_NOTIFICATIONS) ||
                ContextCompat.checkSelfPermission(activity, Manifest.permission.POST_NOTIFICATIONS) !=
                PackageManager.PERMISSION_GRANTED
            ) {
                activity.requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS), REQ_NOTIFICATIONS)
            } else {
                openNotificationSettings(activity)
            }
        }
    }

    // ---------------------------------------------------------------- صفحات الإعدادات

    fun openAppSettings(ctx: Context) = start(
        ctx, Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.parse("package:${ctx.packageName}"))
    )

    fun openNotificationSettings(ctx: Context) = start(
        ctx, Intent(Settings.ACTION_APP_NOTIFICATION_SETTINGS).putExtra(Settings.EXTRA_APP_PACKAGE, ctx.packageName)
    )

    fun openInstallSourcesSettings(ctx: Context) = start(
        ctx, Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:${ctx.packageName}"))
    )

    private fun start(ctx: Context, intent: Intent) {
        runCatching {
            if (ctx !is Activity) intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            ctx.startActivity(intent)
        }.onFailure { runCatching { openAppFallback(ctx) } }
    }

    private fun openAppFallback(ctx: Context) {
        val i = Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.parse("package:${ctx.packageName}"))
        if (ctx !is Activity) i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        ctx.startActivity(i)
    }

    // ---------------------------------------------------------------- مركز الأذونات

    private class PermRow(val title: Int, val desc: Int, val granted: Boolean, val action: (() -> Unit)?)

    /** نافذة تعرض حالة كل إذن وزراً للسماح/فتح الإعدادات. تُفتح من الإعدادات. */
    fun showCenter(activity: Activity) {
        val d = activity.resources.displayMetrics.density
        fun px(v: Int) = (v * d).toInt()
        val textColor = ContextCompat.getColor(activity, R.color.rin_on_toolbar)
        val dimColor = ContextCompat.getColor(activity, R.color.rin_on_toolbar_dim)
        val accent = ContextCompat.getColor(activity, R.color.rin_accent)

        val rows = listOf(
            PermRow(R.string.perm_notifications, R.string.perm_notifications_desc, notificationsGranted(activity)) {
                val asked = activity.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getBoolean(KEY_ASKED_NOTIF, false)
                if (!asked) {
                    activity.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putBoolean(KEY_ASKED_NOTIF, true).apply()
                    requestNotifications(activity)
                } else {
                    openNotificationSettings(activity)
                }
            },
            PermRow(R.string.perm_install, R.string.perm_install_desc, canInstallApks(activity)) {
                openInstallSourcesSettings(activity)
            },
            PermRow(R.string.perm_storage, R.string.perm_storage_desc, true, null),
            PermRow(R.string.perm_network, R.string.perm_network_desc, true, null)
        )

        val box = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(px(20), px(16), px(20), px(8))
        }
        box.addView(TextView(activity).apply {
            setText(R.string.perm_center_hint)
            textSize = 12.5f
            setTextColor(dimColor)
            setPadding(0, 0, 0, px(10))
        })

        var dlg: AlertDialog? = null
        for (r in rows) {
            val row = LinearLayout(activity).apply {
                orientation = LinearLayout.VERTICAL
                setPadding(0, px(10), 0, px(10))
            }
            val head = LinearLayout(activity).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = Gravity.CENTER_VERTICAL
            }
            head.addView(TextView(activity).apply {
                setText(r.title)
                textSize = 15f
                setTypeface(typeface, Typeface.BOLD)
                setTextColor(textColor)
                layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
            })
            head.addView(TextView(activity).apply {
                setText(if (r.granted) R.string.perm_granted else R.string.perm_not_granted)
                textSize = 12.5f
                setTextColor(if (r.granted) accent else ContextCompat.getColor(activity, R.color.log_kind_error))
            })
            row.addView(head)
            row.addView(TextView(activity).apply {
                setText(r.desc)
                textSize = 12.5f
                setTextColor(dimColor)
                setPadding(0, px(3), 0, 0)
            })
            if (!r.granted && r.action != null) {
                row.addView(Button(activity).apply {
                    setText(R.string.perm_allow)
                    setOnClickListener { dlg?.dismiss(); r.action.invoke() }
                })
            }
            box.addView(row)
        }

        // أدوات الإشعارات: تخصيص القنوات (صوت/أهمية كل نوع) وإشعار تجريبي.
        box.addView(Button(activity).apply {
            setText(R.string.perm_notif_channels)
            setOnClickListener { openNotificationSettings(activity) }
        })
        box.addView(Button(activity).apply {
            setText(R.string.perm_notif_test)
            setOnClickListener {
                if (!com.dlof.rinlang.notify.RinNotifier.sendTest(activity)) {
                    android.widget.Toast.makeText(activity, R.string.perm_notif_test_denied, android.widget.Toast.LENGTH_LONG).show()
                }
            }
        })

        dlg = AlertDialog.Builder(activity)
            .setTitle(R.string.perm_center_title)
            .setView(ScrollView(activity).apply { addView(box) })
            .setNeutralButton(R.string.perm_app_settings) { _, _ -> openAppSettings(activity) }
            .setPositiveButton(R.string.dl_close, null)
            .show()
    }
}
