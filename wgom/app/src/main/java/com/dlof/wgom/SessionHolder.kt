package com.dlof.wgom

/**
 * حامل جلسة بسيط في الذاكرة فقط (بلا حفظ دائم) — يكفي لتمرير idToken/uid بين
 * [LoginActivity] و [AccountActivity] خلال نفس تشغيلة التطبيق. عند إغلاق WGOM
 * يُطلب من المستخدم تسجيل الدخول من جديد، وهو سلوك مقصود لتطبيق إدارة حساب حسّاس.
 */
object SessionHolder {
    var session: AuthSession? = null
    var email: String = ""

    fun clear() {
        session = null
        email = ""
    }
}
