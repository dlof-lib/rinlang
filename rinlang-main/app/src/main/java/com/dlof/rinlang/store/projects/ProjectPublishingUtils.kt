package com.dlof.rinlang.store.projects

import android.content.Context
import android.util.Base64
import com.dlof.rinlang.Project
import com.dlof.rinlang.ProjectManager

/**
 * منطق "تجميع" مشروع محلي جاهزاً للنشر في معرض مشاريع Rin: يضغط مجلد المشروع كاملاً عبر
 * [ProjectManager.exportProjectAsZip] الموجودة أصلاً (تُستخدَم أيضاً لتصدير/مشاركة مشروع)، ثم
 * يرمّزه base64 بنفس أسلوب
 * [com.dlof.rinlang.store.extensions.ExtensionPackagingUtils.encodeFileToBase64] — بلا Firebase
 * Storage مدفوع.
 */
object ProjectPublishingUtils {

    /** يضغط [project] ويرمّزه base64، ويرجع النتيجة مع حجم الأرشيف الخام (لتطبيق [com.dlof.rinlang.store.PublishPolicy.validateSize] قبل الترميز). */
    fun buildZipBase64(context: Context, project: Project): Pair<String, Long> {
        val zipFile = ProjectManager.exportProjectAsZip(context, project)
        val rawSize = zipFile.length()
        val base64 = Base64.encodeToString(zipFile.readBytes(), Base64.NO_WRAP)
        return base64 to rawSize
    }
}
