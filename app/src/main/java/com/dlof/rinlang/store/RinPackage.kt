package com.dlof.rinlang.store

/**
 * حزمة منشورة في متجر Rin. تُخزَّن العناصر تحت /packages/{packageId} في Realtime Database.
 * [base64Data] هو أرشيف zip كامل (ملف المكتبة + README.md + LICENSE + أي صور/أيقونات
 * اختارها الناشر) مُرمَّز base64 داخل حقل نصي واحد — بلا Firebase Storage مدفوع.
 *
 * constructor بلا معاملات مطلوب لإعادة البناء التلقائي عبر DataSnapshot.getValue(...).
 *
 * تُنفِّذ java.io.Serializable (بدل Parcelable) لتفادي إضافة plugin kotlin-parcelize؛ يُستخدم
 * هذا فقط لتمرير الحزمة كاملة من شاشة المتجر إلى شاشة تفاصيل الحزمة (PackageDetailActivity)
 * عبر Intent، دون أي أثر على التخزين في Realtime Database.
 */
data class RinPackage(
    val id: String = "",
    val name: String = "",
    val version: String = "1.0.0",
    val description: String = "",
    val license: String = "MIT",
    val publisherUid: String = "",
    val publisherName: String = "",
    val fileName: String = "",   // مثال: mylib.og.rinsdk
    val sizeBytes: Long = 0L,
    val downloadCount: Long = 0L,
    val createdAt: Long = 0L,
    val base64Data: String = "",
    /** تصنيف الحزمة لعرضها ضمن فلاتر المتجر (مثال: "رياضيات"، "نصوص"، "بيانات"). */
    val category: String = "عام",
    /** مجموع كل تقييمات المستخدمين (1-5) وعددها، لحساب المتوسط: ratingSum / ratingCount. */
    val ratingSum: Long = 0L,
    val ratingCount: Long = 0L,
    /** عدد بلاغات المستخدمين عن هذه الحزمة (محتوى مسيء، كود خبيث، انتهاك ترخيص...). */
    val reportCount: Long = 0L,
    /** عدد إعجابات المستخدمين بهذه الحزمة (زر القلب في صفحة التفاصيل). */
    val likeCount: Long = 0L,
    /**
     * تبعيات الحزمة: اسم الحزمة المطلوبة -> شرط الإصدار (مثال: "^1.2.0"، ">=2.0.0"، "1.0.0").
     * تُتحقَّق هذه الشروط عند التثبيت مقابل المكتبات المثبَّتة فعلياً في مشروع المستخدم.
     */
    val dependencies: Map<String, String> = emptyMap()
) : java.io.Serializable {
    /** متوسط التقييم من 0 إلى 5، أو 0 إن لم يقيّمه أحد بعد. */
    val averageRating: Double
        get() = if (ratingCount <= 0L) 0.0 else ratingSum.toDouble() / ratingCount.toDouble()
}
