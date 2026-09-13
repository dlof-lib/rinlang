package com.dlof.gety

/**
 * نفس مشروع Firebase المستخدم في RinStudio (راجع app/google-services.json هناك).
 * GETY لا يحتاج تسجيل تطبيق منفصل في Firebase Console ولا ملف google-services.json
 * خاص به؛ فهو يتواصل مباشرة عبر REST API العام لـ Realtime Database (نفس الأسلوب
 * المستخدم في EmailJsSender.kt داخل RinStudio)، ويكتب فقط ضمن مسار /pairing_codes
 * الذي تسمح قواعده (database.rules.json) بالقراءة للجميع والكتابة المقيَّدة بشرط
 * امتلاك الـtoken الصحيح غير المنتهي وغير المؤكَّد سابقاً.
 */
object FirebaseConfig {
    const val DATABASE_URL = "https://dlof-massage-default-rtdb.firebaseio.com"
}
