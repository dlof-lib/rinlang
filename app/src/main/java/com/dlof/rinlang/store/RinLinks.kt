package com.dlof.rinlang.store

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import com.dlof.rinlang.R
import com.dlof.rinlang.store.extensions.RinExtension

/**
 * روابط المشاركة لموقع Rin (صفحة واحدة على GitHub Pages، بلا HTML لكل مكتبة):
 *
 *   حساب   : https://dlof-lib.github.io/rinlang/@username
 *   مكتبة  : https://dlof-lib.github.io/rinlang/@username/library.og.rin      (الرسمية: @rin/math.og.rin)
 *   إضافة  : https://dlof-lib.github.io/rinlang/@username/extension.rinex
 *
 * نفس الصيغة يفهمها الموقع (web/libraries.js عبر 404.html) وهذا التطبيق (LinkRouterActivity).
 * الفراغات في الأسماء تصبح "-" (slug)، والأحرف غير الآمنة تُرمَّز بـ percent-encoding.
 */
object RinLinks {
    const val HOST = "dlof-lib.github.io"
    const val BASE = "https://$HOST/rinlang/"
    const val LIB_SUFFIX = ".og.rin"
    const val EXT_SUFFIX = ".rinex"

    sealed class Target {
        data class Profile(val user: String) : Target()
        data class Library(val user: String, val key: String) : Target()
        data class Extension(val user: String, val key: String) : Target()
    }

    fun slug(s: String): String = s.trim().replace(Regex("\\s+"), "-")

    /** مفتاح مقارنة موحَّد (بلا لاحقة، بلا مسافات، أحرف صغيرة) — مطابق لـ libKey في libraries.js. */
    fun key(s: String): String = s.trim()
        .replace(Regex("\\.(og\\.rin(sdk)?|rinex)$", RegexOption.IGNORE_CASE), "")
        .replace(Regex("\\s+"), "-").lowercase()

    private fun seg(s: String) = Uri.encode(slug(s))

    fun profile(username: String) = "$BASE@${seg(username)}"
    fun library(publisher: String, name: String) = "$BASE@${seg(publisher)}/${seg(name)}$LIB_SUFFIX"
    fun extension(developer: String, name: String) = "$BASE@${seg(developer)}/${seg(name)}$EXT_SUFFIX"

    /** الحزم القديمة بلا publisherUsername تستخدم اسم العرض (يجده الموقع والتطبيق عبر publisherName). */
    fun forPackage(pkg: RinPackage) = library(pkg.publisherUsername.ifBlank { pkg.publisherName }, pkg.name)
    fun forExtension(ext: RinExtension) = extension(ext.developerUsername.ifBlank { ext.developer }, ext.name)

    /** يحلّل رابطاً واردًا؛ null إن لم يكن رابط Rin مفهوماً. يقبل المسار الجميل والصيغة القديمة ?@user/lib. */
    fun parse(uri: Uri?): Target? {
        if (uri == null || !HOST.equals(uri.host, ignoreCase = true)) return null
        var segs = (uri.pathSegments ?: emptyList())
        var at = segs.indexOfFirst { it.startsWith("@") }
        if (at < 0) {
            val q = uri.encodedQuery.orEmpty()
            if (!q.startsWith("@")) return null
            segs = q.substringBefore('&').split('/').map { Uri.decode(it) }
            at = 0
        }
        val user = segs[at].removePrefix("@").trim()
        if (user.isEmpty() || user.length > 100) return null
        val lib = segs.getOrNull(at + 1)?.trim().orEmpty()
        if (lib.isEmpty()) return Target.Profile(user)
        val k = key(lib)
        if (k.isEmpty()) return null
        return if (lib.endsWith(EXT_SUFFIX, ignoreCase = true)) Target.Extension(user, k) else Target.Library(user, k)
    }

    fun copy(context: Context, url: String) {
        val cm = context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
        cm.setPrimaryClip(ClipData.newPlainText("Rin link", url))
        Toast.makeText(context, R.string.link_copied, Toast.LENGTH_SHORT).show()
    }

    /**
     * الموقع يولّد الآن صفحة معاينة ثابتة + صورة OG لكل رابط (scripts/build_share_pages.py)، فتُظهر واتساب/تيليجرام
     * بطاقة فيها اسم المكتبة والوصف والإصدار والأرقام. لذلك نرسل «العنوان + الرابط» فقط؛ [subtitle] يبقى في التوقيع
     * للتوافق، ولا يُكرَّر في النص لأن البطاقة تحمله أصلاً.
     */
    fun share(context: Context, url: String, title: String, @Suppress("UNUSED_PARAMETER") subtitle: String = "") {
        val body = title.trim() + "\n" + url
        val send = Intent(Intent.ACTION_SEND).apply {
            type = "text/plain"
            putExtra(Intent.EXTRA_SUBJECT, title)
            putExtra(Intent.EXTRA_TEXT, body)
        }
        context.startActivity(Intent.createChooser(send, context.getString(R.string.link_share)))
    }

    /** حوار موحّد: نسخ الرابط / مشاركته. يُستدعى من زر المشاركة في الشريط العلوي لكل شاشة تفاصيل. */
    fun showDialog(context: Context, title: String, url: String, subtitle: String = "") {
        AlertDialog.Builder(context)
            .setTitle(title)
            .setMessage(url)
            .setPositiveButton(R.string.link_share) { _, _ -> share(context, url, title, subtitle) }
            .setNeutralButton(R.string.link_copy) { _, _ -> copy(context, url) }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }
}
