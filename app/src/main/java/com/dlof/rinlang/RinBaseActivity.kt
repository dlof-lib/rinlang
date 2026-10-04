package com.dlof.rinlang

import android.content.Context
import android.content.res.Resources
import android.view.LayoutInflater
import androidx.appcompat.app.AppCompatActivity
import com.dlof.rinlang.packs.LanguagePacks
import com.dlof.rinlang.packs.PackLayoutInflater

/**
 * قاعدة كل شاشات التطبيق. وظيفتها الوحيدة تطبيق حزمة اللغة المنزَّلة (إن وُجدت للغة الواجهة الحالية)
 * فوق موارد النصوص — انظر [LanguagePacks]. بدون حزمة منزَّلة (أو للعربية المدمجة) تتصرف كـ AppCompatActivity تماماً.
 */
open class RinBaseActivity : AppCompatActivity() {

    private val overlayHolder = arrayOfNulls<Any>(1)
    private var inflaterSource: LayoutInflater? = null
    private var inflaterWrapped: LayoutInflater? = null

    override fun getResources(): Resources {
        val base = super.getResources()
        return LanguagePacks.wrap(this, base, overlayHolder)
    }

    override fun getSystemService(name: String): Any? {
        val service = super.getSystemService(name)
        if (name == Context.LAYOUT_INFLATER_SERVICE && service is LayoutInflater && service !is PackLayoutInflater) {
            val translate = LanguagePacks.retranslatorFor(resources) ?: return service
            if (inflaterSource !== service || inflaterWrapped == null) {
                inflaterSource = service
                inflaterWrapped = PackLayoutInflater(service, this, translate)
            }
            return inflaterWrapped
        }
        return service
    }
}
