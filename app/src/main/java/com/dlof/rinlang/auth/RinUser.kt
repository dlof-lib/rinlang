package com.dlof.rinlang.auth

/**
 * سجل الحساب المخزَّن داخل Realtime Database على المسار /users/{uid}.
 * بيانات الدخول نفسها (الإيميل/كلمة السر) لا تُخزَّن هنا؛ يتولاها Firebase Authentication
 * وحده. هذا الكائن هو "الملف الشخصي" العام المرتبط بنفس uid الذي يمنحه Firebase Auth.
 *
 * ملاحظة: نحتاج constructor بلا معاملات (كل القيم لها قيمة افتراضية) لأن Realtime Database
 * يعيد بناء الكائن تلقائياً بهذه الطريقة عبر DataSnapshot.getValue(RinUser::class.java).
 */
data class RinUser(
    val uid: String = "",
    val name: String = "",
    val username: String = "",
    val email: String = "",
    val verified: Boolean = false,
    val createdAt: Long = 0L,
    /** نبذة قصيرة يعرضها المستخدم في ملفه الشخصي (اختياري). */
    val bio: String = "",
    /**
     * صورة الملف الشخصي مُرمَّزة base64 (JPEG مصغّرة إلى 512×512 كحد أقصى قبل الترميز)،
     * تُخزَّن كنص واحد داخل Realtime Database بنفس أسلوب [com.dlof.rinlang.store.RinPackage.base64Data]
     * — بلا Firebase Storage مدفوع. فارغة يعني عدم وجود صورة، وتُعرض شارة بحرف الاسم الأول بدلاً منها.
     */
    val avatarBase64: String = ""
)
