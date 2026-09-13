package com.dlof.wgom

/**
 * نفس مشروع Firebase المستخدم في RinStudio (dlof-massage). WGOM لا يحتاج تسجيل
 * تطبيق منفصل في Firebase Console: مفتاح الـWeb API هنا عام بطبيعته (نفس المفتاح
 * الموجود داخل app/google-services.json في RinStudio، وهو مفتاح قابل للتضمين في
 * تطبيقات العميل حسب توثيق Google نفسه) ويُستخدم فقط لاستدعاء Identity Toolkit
 * (تسجيل الدخول / تغيير كلمة السر) و Realtime Database عبر REST المباشر.
 *
 * إن غيّرت مشروع Firebase مستقبلاً، حدّث القيمتين التاليتين لتطابقا القيم الموجودة
 * في app/google-services.json الخاص بـ RinStudio (project_info.firebase_url وapi_key).
 */
object FirebaseConfig {
    const val DATABASE_URL = "https://dlof-massage-default-rtdb.firebaseio.com"
    const val WEB_API_KEY = "AIzaSyDY5CTUtM5DgP7hvBIWdvEQ9jqE3lE3vSg"
}
