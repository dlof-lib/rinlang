package com.dlof.rinlang.auth

/**
 * إعدادات خدمة الإيميل المجانية EmailJS (https://www.emailjs.com) — تُستخدم لإرسال كود
 * التحقق المكوَّن من 5 أرقام إلى بريد المستخدم مباشرة من التطبيق، بدون أي خادم خاص بنا
 * وبدون ترقية خطة Firebase (Spark تبقى مجانية بالكامل).
 *
 * خطوات الإعداد (مرة واحدة فقط، تستغرق دقائق):
 *  1) أنشئ حساباً مجانياً على emailjs.com (الخطة المجانية تكفي: 200 إيميل/شهر).
 *  2) من Email Services أضِف خدمة بريدك (Gmail/Outlook..) وستحصل على SERVICE_ID.
 *  3) من Email Templates أنشئ قالباً جديداً يحتوي المتغيرات {{to_name}} و {{code}}
 *     ضمن نص الرسالة، وستحصل على TEMPLATE_ID.
 *  4) من Account -> General خذ Public Key.
 *  5) مهم جداً: من Account -> Security فعّل خيار "Allow non-browser calls"، لأن
 *     الاستدعاء هنا يأتي من تطبيق أندرويد وليس من متصفح ويب.
 *  6) الصق القيم الثلاث أدناه بدل النصوص التوضيحية.
 */
object EmailJsConfig {
    const val SERVICE_ID = "PASTE_YOUR_EMAILJS_SERVICE_ID"
    const val TEMPLATE_ID = "PASTE_YOUR_EMAILJS_TEMPLATE_ID"
    const val PUBLIC_KEY = "PASTE_YOUR_EMAILJS_PUBLIC_KEY"

    /** يصبح true تلقائياً بمجرد استبدال القيم الثلاث أعلاه بقيمك الحقيقية. */
    val isConfigured: Boolean
        get() = SERVICE_ID.startsWith("PASTE_").not() &&
            TEMPLATE_ID.startsWith("PASTE_").not() &&
            PUBLIC_KEY.startsWith("PASTE_").not()
}
