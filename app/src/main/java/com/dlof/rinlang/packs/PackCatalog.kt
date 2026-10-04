package com.dlof.rinlang.packs

import android.content.Context
import android.os.Handler
import android.os.Looper
import com.dlof.rinlang.store.RinLinks
import org.json.JSONObject
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import java.util.concurrent.Executors

/**
 * قائمة الحزم المتاحة للتنزيل (catalog.json المنشور على GitHub Pages بواسطة scripts/build_content_packs.py
 * عبر .github/workflows/pages.yml). تُخزَّن نسخة محلية للعرض دون اتصال.
 */
object PackCatalog {

    /** يمكن تغييره لاختبار كتالوج محلي/تجريبي. */
    @Volatile
    var catalogUrl: String = RinLinks.BASE + "packs/catalog.json"

    private const val CACHE_NAME = "catalog.json"
    private const val MAX_CATALOG_BYTES = 1024 * 1024
    private val executor = Executors.newSingleThreadExecutor()
    private val main = Handler(Looper.getMainLooper())

    private fun cacheFile(context: Context) = File(PackStore.root(context), CACHE_NAME)

    /** آخر كتالوج محفوظ محلياً (فارغ إن لم يُجلب قط). */
    fun cached(context: Context): List<ContentPack> {
        val f = cacheFile(context)
        if (!f.isFile) return emptyList()
        return try {
            parse(f.readText(Charsets.UTF_8))
        } catch (e: Exception) {
            emptyList()
        }
    }

    fun find(context: Context, id: String): ContentPack? = cached(context).firstOrNull { it.id == id }

    /**
     * يجلب الكتالوج من الشبكة في خيط خلفي ويعيد النتيجة على الخيط الرئيسي. عند الفشل تُعاد
     * القائمة المحفوظة مع الخطأ حتى تبقى الشاشة قابلة للاستخدام دون اتصال.
     */
    fun refresh(context: Context, onResult: (packs: List<ContentPack>, error: Throwable?) -> Unit) {
        val app = context.applicationContext
        executor.execute {
            var error: Throwable? = null
            var packs: List<ContentPack>
            try {
                val text = download()
                packs = parse(text)
                cacheFile(app).writeText(text, Charsets.UTF_8)
            } catch (t: Throwable) {
                error = t
                packs = cached(app)
            }
            main.post { onResult(packs, error) }
        }
    }

    /** نسخة متزامنة تُستدعى من خيط خلفي فقط (مثلاً قبل تنزيل حزمة طُلبت قبل أن يُجلب الكتالوج). */
    @Throws(Exception::class)
    fun refreshBlocking(context: Context): List<ContentPack> {
        val text = download()
        val packs = parse(text)
        cacheFile(context).writeText(text, Charsets.UTF_8)
        return packs
    }

    private fun download(): String {
        val conn = URL(catalogUrl).openConnection() as HttpURLConnection
        try {
            conn.connectTimeout = 15_000
            conn.readTimeout = 20_000
            conn.setRequestProperty("Accept", "application/json")
            conn.setRequestProperty("Cache-Control", "no-cache")
            val code = conn.responseCode
            if (code != 200) throw PackException(PackException.Kind.NETWORK, "catalog HTTP $code")
            val out = java.io.ByteArrayOutputStream()
            conn.inputStream.use { input ->
                val buf = ByteArray(8192)
                while (true) {
                    val n = input.read(buf)
                    if (n < 0) break
                    out.write(buf, 0, n)
                    if (out.size() > MAX_CATALOG_BYTES) throw PackException(PackException.Kind.CORRUPT, "catalog too large")
                }
            }
            return out.toString("UTF-8")
        } finally {
            conn.disconnect()
        }
    }

    private fun parse(text: String): List<ContentPack> {
        val arr = JSONObject(text).getJSONArray("packs")
        val list = ArrayList<ContentPack>()
        for (i in 0 until arr.length()) {
            ContentPack.fromJson(arr.getJSONObject(i), catalogUrl)?.let { list.add(it) }
        }
        return list
    }
}
