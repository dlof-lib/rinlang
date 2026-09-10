package com.dlof.rinlang

import android.app.Activity
import android.content.Intent
import android.net.Uri
import android.widget.Toast
import androidx.core.content.FileProvider
import java.io.File

/**
 * إجراءات تثبيت/مشاركة/حفظ حزمة APK مُصدَّرة — نفس الأسلوب المستخدم أصلاً داخل
 * [ApkExportActivity]، مُستخرَج هنا حتى تستخدمه أيضاً شاشتا "تطبيقات Rin"
 * ([RinAppsActivity], [RinAppSettingsActivity]) بلا تكرار.
 */
object ApkInstallActions {

    private fun apkUri(activity: Activity, file: File): Uri =
        FileProvider.getUriForFile(activity, "${activity.packageName}.fileprovider", file)

    fun install(activity: Activity, file: File) {
        try {
            val uri = apkUri(activity, file)
            val intent = Intent(Intent.ACTION_VIEW).apply {
                setDataAndType(uri, "application/vnd.android.package-archive")
                addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            }
            activity.startActivity(intent)
        } catch (e: Exception) {
            ErrorReporter.report(activity, e, "APK_INSTALL")
        }
    }

    fun share(activity: Activity, file: File) {
        try {
            val uri = apkUri(activity, file)
            val intent = Intent(Intent.ACTION_SEND).apply {
                type = "application/vnd.android.package-archive"
                putExtra(Intent.EXTRA_STREAM, uri)
                addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            }
            activity.startActivity(Intent.createChooser(intent, activity.getString(R.string.apk_export_action_share)))
        } catch (e: Exception) {
            ErrorReporter.report(activity, e, "APK_SHARE")
        }
    }

    fun saveToDownloads(activity: Activity, file: File) {
        val artifact = RinArtifact(
            kind = ArtifactKind.APK,
            relPath = file.name,
            absoluteFile = file,
            sizeBytes = file.length()
        )
        RinDownloadManager.downloadToPublicDownloads(
            activity = activity,
            artifact = artifact,
            onProgress = { _, _ -> },
            onDone = { uri, error ->
                if (error != null || uri == null) {
                    Toast.makeText(activity, error?.message ?: "error", Toast.LENGTH_SHORT).show()
                } else {
                    Toast.makeText(activity, activity.getString(R.string.apk_export_action_save), Toast.LENGTH_SHORT).show()
                }
            }
        )
    }
}
