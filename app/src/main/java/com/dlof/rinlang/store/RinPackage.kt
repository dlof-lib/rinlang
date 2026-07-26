package com.dlof.rinlang.store

/**
 * حزمة منشورة في متجر Rin. تُخزَّن العناصر تحت /packages/{packageId} في Realtime Database.
 * [base64Data] هو أرشيف zip كامل (ملف المكتبة + README.md + LICENSE + أي صور/أيقونات
 * اختارها الناشر) مُرمَّز base64 داخل حقل نصي واحد — بلا Firebase Storage مدفوع.
 *
 * constructor بلا معاملات مطلوب لإعادة البناء التلقائي عبر DataSnapshot.getValue(...).
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
    val base64Data: String = ""
)
