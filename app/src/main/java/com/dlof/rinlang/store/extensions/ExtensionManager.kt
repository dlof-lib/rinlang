package com.dlof.rinlang.store.extensions

import android.content.Context
import android.util.Base64
import java.io.ByteArrayInputStream
import java.io.File
import java.util.zip.ZipInputStream

/** نتيجة عملية تثبيت/تحديث إضافة. */
sealed class ExtensionInstallResult {
    data class Success(val record: InstalledExtensionRecord) : ExtensionInstallResult()
    data class Failure(val message: String) : ExtensionInstallResult()
}

/**
 * يدير دورة حياة الإضافات محلياً على الجهاز: التثبيت (فك أرشيف zip المرمَّز base64 داخل
 * [RinExtension.base64Data] إلى مجلد خاص بالإضافة)، التحديث، الإزالة، والتفعيل/التعطيل.
 *
 * الإضافات مخزَّنة على مستوى التطبيق كله (filesDir/rin_extensions/<id>/) وليست خاصة بمشروع
 * واحد كما هو حال مكتبات لغة Rin — لأن الإضافة تخصّ المحرر/الأدوات نفسها، لا مشروعاً بعينه.
 */
object ExtensionManager {

    private const val ROOT_DIR_NAME = "rin_extensions"

    fun extensionsRoot(context: Context): File {
        val root = File(context.filesDir, ROOT_DIR_NAME)
        if (!root.exists()) root.mkdirs()
        return root
    }

    private fun extensionDir(context: Context, extensionId: String): File =
        File(extensionsRoot(context), extensionId)

    fun isInstalled(context: Context, extensionId: String): Boolean =
        InstalledExtensionsIndex.get(context, extensionId) != null

    fun listInstalled(context: Context): List<InstalledExtensionRecord> =
        InstalledExtensionsIndex.readAll(context).values.sortedByDescending { it.installedAt }

    fun installedRecord(context: Context, extensionId: String): InstalledExtensionRecord? =
        InstalledExtensionsIndex.get(context, extensionId)

    /**
     * يثبِّت [ext]: يتحقق أولاً من تطابق التوقيع الرقمي المعلَن مع محتوى الأرشيف الفعلي (سلامة
     * البيانات)، ثم يفكّ الأرشيف إلى مجلد الإضافة، ثم يكتب extension.rinext، ثم يسجّلها في
     * فهرس الإضافات المثبَّتة كمفعَّلة افتراضياً. [onProgress] يُستدعى بنسبة 0..100 أثناء الفك.
     */
    fun install(
        context: Context,
        ext: RinExtension,
        onProgress: (Int) -> Unit = {},
        callback: (ExtensionInstallResult) -> Unit
    ) {
        if (ext.base64Data.isBlank()) {
            callback(ExtensionInstallResult.Failure("لا يوجد محتوى لهذه الإضافة"))
            return
        }
        if (ext.signature.isNotBlank() && !ExtensionPermissions.verifySignature(ext.base64Data, ext.signature)) {
            callback(ExtensionInstallResult.Failure("فشل التحقق من التوقيع الرقمي — قد يكون محتوى الإضافة تالفاً أو مُعدَّلاً"))
            return
        }

        val dir = extensionDir(context, ext.id)
        try {
            dir.deleteRecursively()
            dir.mkdirs()

            val zipBytes = Base64.decode(ext.base64Data, Base64.DEFAULT)
            extractZip(zipBytes, dir, onProgress)
            ExtensionManifestFile.write(dir, ext)

            val record = InstalledExtensionRecord(
                id = ext.id,
                name = ext.name,
                version = ext.version,
                type = ext.type,
                developer = ext.developer,
                enabled = true,
                installedAt = System.currentTimeMillis()
            )
            InstalledExtensionsIndex.upsert(context, record)
            onProgress(100)
            ExtensionRepository.incrementDownloadCount(ext.id)
            callback(ExtensionInstallResult.Success(record))
        } catch (t: Throwable) {
            dir.deleteRecursively()
            callback(ExtensionInstallResult.Failure(t.message ?: "تعذّر تثبيت الإضافة"))
        }
    }

    /** يحدِّث إضافة مثبَّتة مسبقاً إلى إصدار [ext] الجديد، مع الحفاظ على حالة التفعيل الحالية. */
    fun update(
        context: Context,
        ext: RinExtension,
        onProgress: (Int) -> Unit = {},
        callback: (ExtensionInstallResult) -> Unit
    ) {
        val wasEnabled = InstalledExtensionsIndex.get(context, ext.id)?.enabled ?: true
        install(context, ext, onProgress) { result ->
            if (result is ExtensionInstallResult.Success && !wasEnabled) {
                InstalledExtensionsIndex.setEnabled(context, ext.id, false)
            }
            callback(result)
        }
    }

    /** يزيل إضافة مثبَّتة: يحذف مجلدها بالكامل ويشطبها من فهرس الإضافات المثبَّتة. */
    fun uninstall(context: Context, extensionId: String): Boolean {
        val deleted = extensionDir(context, extensionId).deleteRecursively()
        InstalledExtensionsIndex.remove(context, extensionId)
        return deleted
    }

    fun setEnabled(context: Context, extensionId: String, enabled: Boolean) {
        InstalledExtensionsIndex.setEnabled(context, extensionId, enabled)
    }

    /** يفكّ أرشيف zip بايتاً إلى [targetDir]، مع تقرير تقدّم تقريبي حسب عدد الإدخالات المُعالَجة. */
    private fun extractZip(zipBytes: ByteArray, targetDir: File, onProgress: (Int) -> Unit) {
        ZipInputStream(ByteArrayInputStream(zipBytes)).use { zis ->
            // نمرّ أولاً لعدّ الإدخالات لحساب نسبة تقدّم حقيقية بدل شريط وهمي.
            var entry = zis.nextEntry
            var processed = 0
            var total = 0
            val buffered = mutableListOf<Pair<java.util.zip.ZipEntry, ByteArray>>()
            while (entry != null) {
                val bytes = zis.readBytes()
                buffered.add(entry to bytes)
                total++
                entry = zis.nextEntry
            }
            if (total == 0) { onProgress(100); return }
            buffered.forEach { (zipEntry, bytes) ->
                val outFile = File(targetDir, zipEntry.name)
                if (zipEntry.isDirectory) {
                    outFile.mkdirs()
                } else {
                    outFile.parentFile?.mkdirs()
                    outFile.writeBytes(bytes)
                }
                processed++
                onProgress((processed * 100) / total)
            }
        }
    }
}
