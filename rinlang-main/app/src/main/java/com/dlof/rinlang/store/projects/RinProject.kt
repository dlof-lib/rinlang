package com.dlof.rinlang.store.projects

/**
 * مشروع Rin كامل منشور في "معرض مشاريع Rin" (Rin Projects Gallery) — قسم مستقل عن متجر
 * المكتبات ([com.dlof.rinlang.store.RinPackage]) ومتجر الإضافات
 * ([com.dlof.rinlang.store.extensions.RinExtension]): هنا يُشارك المستخدم **مشروعاً كاملاً**
 * أنشأه بنفسه (كل ملفاته .rin ومكتباته المحلية)، ليتصفّحه ويحمّله بقية مجتمع Rin كمصدر إلهام
 * أو نقطة انطلاق لمشروعهم الخاص. تُخزَّن العناصر تحت /rin_projects/{projectId} في Realtime
 * Database، بنفس فلسفة بقية أقسام المتجر: قراءة عامة، وكتابة مقصورة على صاحب المشروع فقط
 * (راجع firebase/database.rules.json).
 *
 * [zipBase64] هو أرشيف مشروع المستخدم كاملاً (راجع
 * [com.dlof.rinlang.ProjectManager.exportProjectAsZip]) مُرمَّز base64 داخل حقل نصي واحد.
 *
 * [thumbnailBase64] ليست صورة يرفعها الناشر، بل صورة تُولَّد تلقائياً بواسطة
 * [CodeThumbnailGenerator] من **الكود الحقيقي** لأحد ملفات المشروع نفسه (لقطة شبيهة بمحرر Rin
 * بألوان الصيغة النحوية الفعلية) — بحيث تعكس بطاقة كل مشروع في المعرض جزءاً حقيقياً من كوده،
 * لا صورة تسويقية مصطنعة.
 *
 * constructor بلا معاملات مطلوب لإعادة البناء التلقائي عبر DataSnapshot.getValue(...).
 */
data class RinProject(
    val id: String = "",
    val name: String = "",
    val description: String = "",
    val publisherUid: String = "",
    val publisherName: String = "",
    val createdAt: Long = 0L,
    /** عدد ملفات .rin (وملفات أخرى) داخل المشروع وقت النشر، لعرضه في بطاقة المعرض. */
    val fileCount: Int = 0,
    val sizeBytes: Long = 0L,
    val downloadCount: Long = 0L,
    val likeCount: Long = 0L,
    /** تصنيف اختياري لتسهيل التصفّح (مثال: "لعبة"، "أداة"، "موقع"، "تجربة"). */
    val category: String = "عام",
    /** أرشيف zip كامل لملفات المشروع، مُرمَّز base64 — راجع توثيق الصنف أعلاه. */
    val zipBase64: String = "",
    /**
     * صورة مصغّرة مُولَّدة تلقائياً من كود المشروع الحقيقي (base64 JPEG) — راجع
     * [CodeThumbnailGenerator]. تُعرَض كبانر أعلى بطاقة المشروع في المعرض بدل أي صورة افتراضية.
     */
    val thumbnailBase64: String = "",
    /** اسم الملف .rin الذي وُلِّدت منه [thumbnailBase64]، يُعرَض كتلميح صغير أسفل الصورة. */
    val thumbnailSourceFile: String = ""
) : java.io.Serializable {

    /** متوسط إعجاب تقريبي مبسَّط: لا يوجد تقييم بالنجوم هنا (بخلاف الحزم والإضافات)، فقط
     *  عدّاد إعجابات مباشر، لأن "المشروع" تجربة تُعجَب بها ككل لا تُقيَّم بمعايير جزئية. */
    val hasLikes: Boolean
        get() = likeCount > 0L
}
