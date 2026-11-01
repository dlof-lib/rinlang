package com.dlof.rinlang

import androidx.appcompat.app.AppCompatDelegate
import androidx.core.os.LocaleListCompat

/**
 * يتحكم بلغة واجهة التطبيق (عربي/إنجليزي/إسباني) باستخدام Per-App Language API الرسمية
 * من AndroidX (AppCompatDelegate.setApplicationLocales) بدل إعادة كتابة attachBaseContext
 * في كل Activity على حدة. تُخزَّن القيمة تلقائياً بواسطة AppCompat (انظر AndroidManifest.xml:
 * meta-data "autoStoreLocales") وتُطبَّق تلقائياً في كل مرة يُفتح فيها التطبيق من جديد،
 * كما تعمل مع "لغة التطبيق" النظامية في إعدادات أندرويد 13+ تلقائياً.
 */
object LocaleHelper {

    const val LANG_ARABIC = "ar"
    const val LANG_ENGLISH = "en"
    const val LANG_SPANISH = "es"

    /** يغيّر لغة التطبيق فوراً؛ تُعاد بناء كل الشاشات المفتوحة تلقائياً لتطبيق اللغة الجديدة. */
    fun setAppLocale(languageTag: String) {
        val locales = LocaleListCompat.forLanguageTags(languageTag)
        AppCompatDelegate.setApplicationLocales(locales)
    }

    /** يعيد رمز اللغة الحالية المطبَّقة فعلياً (ar/en/es)، أو null إن كان الوضع "تلقائي حسب النظام". */
    fun getCurrentAppLocaleTag(): String? {
        val locales = AppCompatDelegate.getApplicationLocales()
        if (locales.isEmpty) return null
        return locales[0]?.language
    }
}
