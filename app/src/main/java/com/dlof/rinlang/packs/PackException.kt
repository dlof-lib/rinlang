package com.dlof.rinlang.packs

import android.content.Context
import com.dlof.rinlang.R

/** خطأ تنزيل/تثبيت حزمة، بنوع قابل للترجمة لرسالة مستخدم عبر [userMessage]. */
class PackException(val kind: Kind, message: String, cause: Throwable? = null) : Exception(message, cause) {

    enum class Kind { OFFLINE, NETWORK, HASH, CORRUPT, STORAGE, CANCELLED, UNKNOWN_PACK }

    fun userMessage(context: Context): String = when (kind) {
        Kind.OFFLINE -> context.getString(R.string.packs_error_format, context.getString(R.string.packs_error_offline))
        Kind.HASH -> context.getString(R.string.packs_error_hash)
        Kind.CANCELLED -> context.getString(R.string.packs_cancelled)
        Kind.UNKNOWN_PACK -> context.getString(R.string.packs_error_unknown_pack)
        else -> context.getString(R.string.packs_error_format, message ?: kind.name)
    }
}
