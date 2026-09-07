package com.dlof.rinlang.apk

import java.io.BufferedOutputStream
import java.io.File
import java.io.FileOutputStream
import java.io.OutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.zip.CRC32
import java.util.zip.ZipEntry
import java.util.zip.ZipFile
import java.util.zip.ZipOutputStream

/**
 * يعيد بناء حزمة APK حقيقية من حزمة RinStudio المضيفة نفسها (الملف الثنائي المثبَّت فعلياً
 * على الجهاز — نفس classes.dex ومكتبات JNI المُصرَّفة وresources.arsc، لا حاجة لأي مُصرِّف
 * Dex أو aapt2 على الجهاز، لأن الشيفرة القابلة للتنفيذ (ExportedRunActivity + محرّك Rin
 * الأصلي) مُدمجة أصلاً في نفس ثنائي RinStudio):
 *
 *  1. ينسخ كل مُدخلات zip من الحزمة المضيفة بنفس أسلوب الضغط (STORED/DEFLATED) —
 *     باستثناء AndroidManifest.xml (يُستبدل بالنسخة المُعدَّلة من [AxmlManifestPatcher])
 *     وملفات توقيع META-INF القديمة (MANIFEST.MF, أو أي ملف SF أو RSA) لأنها ستُستبدل بتوقيع جديد.
 *  2. يحقن ملفات مشروع Rin كموارد assets/rin_export_project/ (كل الملفات) + بيان JSON صغير يقرأه
 *     [com.dlof.rinlang.ExportedRunActivity] عند إقلاع الحزمة المُصدَّرة.
 *  3. يطبّق محاذاة zipalign الحقيقية: المُدخلات غير المضغوطة (STORED) — وتحديداً مكتبات
 *     .so — تُحاذى لحدود 4096 بايت (متطلّب أندرويد الحديث لتحميلها عبر mmap مباشرة من
 *     الحزمة)، وبقية مُدخلات STORED تُحاذى لحدود 4 بايت، عبر حقل "extra" القياسي في محلي
 *     رأس كل مُدخل (نفس الآلية التي تستخدمها أداة zipalign الرسمية).
 */
object ApkRepackager {

    private const val ALIGN_DEFAULT = 4
    private const val ALIGN_SO = 4096

    private val SIGNATURE_FILE_REGEX = Regex("META-INF/(MANIFEST\\.MF|.*\\.(SF|RSA|DSA|EC))", RegexOption.IGNORE_CASE)

    private class CountingOutputStream(private val out: OutputStream) : OutputStream() {
        var count: Long = 0L
            private set
        override fun write(b: Int) { out.write(b); count++ }
        override fun write(b: ByteArray, off: Int, len: Int) { out.write(b, off, len); count += len }
        override fun flush() = out.flush()
        override fun close() = out.close()
    }

    /** ملف إضافي يُحقن داخل الحزمة الناتجة (اسم بمسار zip كامل + محتوى خام). */
    data class ExtraEntry(val name: String, val data: ByteArray)

    fun build(
        hostApkFile: File,
        patchedManifest: ByteArray,
        extraEntries: List<ExtraEntry>,
        outFile: File
    ) {
        outFile.parentFile?.mkdirs()
        val counting = CountingOutputStream(BufferedOutputStream(FileOutputStream(outFile)))
        val zos = ZipOutputStream(counting)
        zos.setLevel(9)

        fun writeEntry(name: String, data: ByteArray, method: Int, alignTo: Int) {
            val nameBytes = name.toByteArray(Charsets.UTF_8)
            var extra: ByteArray? = null
            if (alignTo > 1) {
                val headerStart = counting.count
                var need = ((alignTo - ((headerStart + 30 + nameBytes.size) % alignTo)) % alignTo).toInt()
                if (need in 1..3) need += alignTo
                if (need > 0) {
                    val bb = ByteBuffer.allocate(need).order(ByteOrder.LITTLE_ENDIAN)
                    bb.putShort(0, 0)                          // header ID (غير مخصَّص/يُتجاهَل بأمان)
                    bb.putShort(2, (need - 4).toShort())        // طول البيانات التالية لهذا الـ TLV
                    extra = bb.array()
                }
            }
            val entry = ZipEntry(name)
            entry.method = method
            if (method == ZipEntry.STORED) {
                entry.size = data.size.toLong()
                entry.compressedSize = data.size.toLong()
                val crc = CRC32(); crc.update(data)
                entry.crc = crc.value
            }
            if (extra != null) entry.setExtra(extra)
            zos.putNextEntry(entry)
            zos.write(data)
            zos.closeEntry()
        }

        ZipFile(hostApkFile).use { zf ->
            val entries = zf.entries()
            while (entries.hasMoreElements()) {
                val e = entries.nextElement()
                if (e.isDirectory) continue
                if (e.name == "AndroidManifest.xml") continue // نكتب النسخة المُعدَّلة لاحقاً
                if (SIGNATURE_FILE_REGEX.matches(e.name)) continue // سيُعاد توقيعها من الصفر

                val data = zf.getInputStream(e).use { it.readBytes() }
                val alignTo = when {
                    e.method != ZipEntry.STORED -> 0
                    e.name.endsWith(".so") -> ALIGN_SO
                    else -> ALIGN_DEFAULT
                }
                writeEntry(e.name, data, e.method, alignTo)
            }
        }

        writeEntry("AndroidManifest.xml", patchedManifest, ZipEntry.DEFLATED, 0)
        for (extra in extraEntries) {
            writeEntry(extra.name, extra.data, ZipEntry.DEFLATED, 0)
        }

        zos.finish()
        zos.close()
    }
}
