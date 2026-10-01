package com.dlof.rinlang

import android.os.Handler
import android.os.Looper
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

/** يُستدعى على الترد الرئيسي فقط. [deliver] آمنة للنداء مرة واحدة (النداءات اللاحقة تُتجاهَل). */
typealias RinInputPresenter = (prompt: String, deliver: (String?) -> Unit) -> Unit

/**
 * جسر إدخال المستخدم لدوال Rin: input() / inputNumber() / confirm() / choose().
 *
 * المحرك الأصلي يستدعي [request] من ترد التشغيل (RinJobWorker) فيُحجَب هذا الترد حتى يجيب
 * المستخدم. العرض نفسه يتم على الترد الرئيسي عبر [presenter] الذي تضبطه MainActivity
 * (نافذة إدخال حقيقية). لا شيء هنا يلمس الواجهة مباشرة.
 *
 * - إلغاء المستخدم => [request] تعيد null => المحرك يوقف البرنامج بخطأ "تم إلغاء الإدخال".
 * - إعادة إنشاء الـ Activity (تدوير الشاشة مثلاً): الطلبات المعلّقة تبقى، وتُعرَض من جديد عند
 *   ضبط presenter جديد، فلا يضيع سؤال قيد الانتظار.
 * - سقف انتظار [MAX_WAIT_MS] كي لا يبقى ترد التشغيل معلّقاً للأبد إن غادر المستخدم التطبيق.
 */
object RinInputBridge {

    /** أقصى مدة انتظار لإجابة واحدة قبل اعتبارها إلغاءً. */
    private const val MAX_WAIT_MS = 10 * 60 * 1000L

    private class Pending(val prompt: String) {
        val latch = CountDownLatch(1)
        @Volatile var answer: String? = null
        @Volatile var done = false
        @Volatile var shown = false
    }

    private val main = Handler(Looper.getMainLooper())
    private val pending = CopyOnWriteArrayList<Pending>()
    @Volatile private var presenter: RinInputPresenter? = null

    /** تضبطه الواجهة النشطة؛ يعرض فوراً أي أسئلة معلّقة لم تُعرَض بعد. */
    fun setPresenter(p: RinInputPresenter) {
        presenter = p
        main.post {
            pending.filter { !it.done }.forEach { present(it) }
        }
    }

    /**
     * تستدعيه الواجهة عند الإغلاق بنفس الـ [p] الذي ضبطته. لا يفعل شيئاً إن كانت واجهة أحدث قد
     * ضبطت presenter آخر قبل وصول onDestroy القديم. الأسئلة المعلّقة تبقى بانتظار واجهة جديدة.
     */
    fun clearPresenter(p: RinInputPresenter) {
        if (presenter !== p) return
        presenter = null
        pending.forEach { it.shown = false }
    }

    private fun present(p: Pending) {
        val pres = presenter ?: return
        if (p.done || p.shown) return
        p.shown = true
        pres(p.prompt) { value ->
            if (!p.done) {
                p.answer = value
                p.done = true
                p.latch.countDown()
            }
        }
    }

    /**
     * يُستدعى من ترد التشغيل. يعيد ما كتبه المستخدم، أو null عند الإلغاء/انتهاء المهلة.
     * تُحجَب هذه الدالة عمداً — لا تستدعها من الترد الرئيسي.
     */
    fun request(prompt: String): String? {
        val p = Pending(prompt)
        pending.add(p)
        main.post { present(p) }
        return try {
            if (!p.latch.await(MAX_WAIT_MS, TimeUnit.MILLISECONDS)) null else p.answer
        } catch (e: InterruptedException) {
            Thread.currentThread().interrupt()
            null
        } finally {
            p.done = true
            pending.remove(p)
        }
    }
}
