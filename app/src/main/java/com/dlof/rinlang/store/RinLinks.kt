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

    fun slug(s: String): String = s.trim().removePrefix("@").replace(Regex("\\s+"), "-").replace(Regex("-+"), "-").trim('-')

    fun key(s: String): String = s.trim()
        .replace(Regex("\\.(og\\.rin(sdk)?|rinex)$", RegexOption.IGNORE_CASE), "")
        .replace(Regex("\\s+"), "-").replace(Regex("-+"), "-").trim('-').lowercase()

    private fun validSegment(s: String): Boolean = s.length in 1..100 && s.matches(Regex("[A-Za-z0-9._~-]+"))
    private fun enc(s: String): String = Uri.encode(s).replace("%40", "@")

    fun profile(username: String): String {
        val u = slug(username); require(validSegment(u)) { "Invalid Rin username" }; return "$BASE@${enc(u)}"
    }

    fun library(publisher: String, name: String): String {
        val u = slug(publisher); val n = slug(name.replace(Regex("\\.og\\.rin$", RegexOption.IGNORE_CASE), ""))
        require(validSegment(u) && validSegment(n)) { "Invalid Rin library reference" }; return "$BASE@${enc(u)}/${enc(n)}$LIB_SUFFIX"
    }

    fun extension(developer: String, name: String): String {
        val u = slug(developer); val n = slug(name.replace(Regex("\\.rinex$", RegexOption.IGNORE_CASE), ""))
        require(validSegment(u) && validSegment(n)) { "Invalid Rin extension reference" }; return "$BASE@${enc(u)}/${enc(n)}$EXT_SUFFIX"
    }

    fun forPackage(pkg: RinPackage) = library(pkg.publisherUsername.ifBlank { pkg.publisherName }, pkg.name)
    fun forExtension(ext: RinExtension) = extension(ext.developerUsername.ifBlank { ext.developer }, ext.name)

    fun parse(uri: Uri?): Target? {
        if (uri == null || !HOST.equals(uri.host, ignoreCase = true)) return null
        val path = uri.pathSegments.orEmpty(); val at = path.indexOfFirst { it.startsWith("@") }
        val segs = if (at >= 0) path.drop(at) else {
            val q = uri.encodedQuery.orEmpty(); if (!q.startsWith("@")) return null
            q.substringBefore('&').split('/').filter { it.isNotEmpty() }.map(Uri::decode)
        }
        if (segs.isEmpty()) return null
        val user = Uri.decode(segs[0]).removePrefix("@").trim(); if (!validSegment(slug(user))) return null
        val lib = segs.getOrNull(1)?.let(Uri::decode)?.trim().orEmpty(); if (lib.isEmpty()) return Target.Profile(user)
        val k = key(lib); if (!validSegment(k)) return null
        return if (lib.endsWith(EXT_SUFFIX, true)) Target.Extension(user, k) else Target.Library(user, k)
    }

    fun copy(context: Context, url: String) {
        val cm = context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
        cm.setPrimaryClip(ClipData.newPlainText("Rin link", url)); Toast.makeText(context, R.string.link_copied, Toast.LENGTH_SHORT).show()
    }

    fun share(context: Context, url: String, title: String, @Suppress("UNUSED_PARAMETER") subtitle: String = "") {
        val send = Intent(Intent.ACTION_SEND).apply { type = "text/plain"; putExtra(Intent.EXTRA_SUBJECT, title); putExtra(Intent.EXTRA_TEXT, title.trim() + "\n" + url) }
        context.startActivity(Intent.createChooser(send, context.getString(R.string.link_share)))
    }

    fun showDialog(context: Context, title: String, url: String, subtitle: String = "") {
        AlertDialog.Builder(context).setTitle(title).setMessage(url)
            .setPositiveButton(R.string.link_share) { _, _ -> share(context, url, title, subtitle) }
            .setNeutralButton(R.string.link_copy) { _, _ -> copy(context, url) }.setNegativeButton(R.string.cancel, null).show()
    }
}
