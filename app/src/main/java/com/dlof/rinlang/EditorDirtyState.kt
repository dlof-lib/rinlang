package com.dlof.rinlang

/**
 * سجل بسيط في الذاكرة فقط (لا يُحفَظ على القرص، يُصفَّر تلقائياً بإعادة تشغيل التطبيق — وهذا
 * مقصود: يمثّل حرفياً "تعديلات مكتوبة الآن في جلسة تحرير ولم تُحفَظ بعد على القرص"، لا أكثر ولا
 * أقل) لملفات فيها تغييرات غير محفوظة، حتى تعرض شاشة "الملفات" (FilesActivity) علامة ● حقيقية
 * مطابقة لواقع الملف، بدل مؤشر وهمي لا معنى حقيقياً له.
 *
 * [MainActivity] يحدّثه: mark() عند كل تعديل نصّي حقيقي (بعد استبعاد التغييرات البرمجية مثل فتح
 * ملف)، و unmark() ضمن markClean() نفسها (نقطة انصهار واحدة تغطي كل مسارات الحفظ: يدوي، تلقائي
 * بعد تهدئة، أو عند onPause). [FilesActivity] يقرأه فقط عند إعادة رسم القائمة عبر dirtyPaths()،
 * بلا أي كتابة من جانبه. المفتاح "اسم المشروع|المسار النسبي" حتى لا يتصادم ملفان بنفس المسار
 * النسبي في مشروعين مختلفين.
 */
object EditorDirtyState {
    private val dirty = mutableSetOf<String>()

    private fun key(projectName: String, relPath: String) = "$projectName|$relPath"

    @Synchronized
    fun mark(projectName: String, relPath: String) {
        dirty += key(projectName, relPath)
    }

    @Synchronized
    fun unmark(projectName: String, relPath: String) {
        dirty -= key(projectName, relPath)
    }

    /** كل المسارات النسبية التي فيها تعديل غير محفوظ حالياً، داخل مشروع [projectName] فقط. */
    @Synchronized
    fun dirtyPaths(projectName: String): Set<String> =
        dirty.filter { it.startsWith("$projectName|") }.map { it.substringAfter('|') }.toSet()
}
