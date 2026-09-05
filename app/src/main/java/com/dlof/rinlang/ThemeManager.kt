package com.dlof.rinlang

import android.content.Context
import androidx.appcompat.app.AppCompatDelegate

/** Applies the editor theme choice consistently across activities. */
object ThemeManager {
    const val SYSTEM = "system"
    const val DARK = "dark"
    const val LIGHT = "light"

    fun apply(context: Context) {
        when (AppSettings.getThemeMode(context)) {
            DARK -> AppCompatDelegate.setDefaultNightMode(AppCompatDelegate.MODE_NIGHT_YES)
            LIGHT -> AppCompatDelegate.setDefaultNightMode(AppCompatDelegate.MODE_NIGHT_NO)
            else -> AppCompatDelegate.setDefaultNightMode(AppCompatDelegate.MODE_NIGHT_FOLLOW_SYSTEM)
        }
    }
}
