package com.dlof.rinlang

/**
 * سجل ثقة الناشرين لحزم RinPak على الجهاز (Trust On First Use).
 *
 * توقيع Ed25519 يثبت أن الحزمة لم تُعدَّل وأن مفتاحاً معيّناً وقّعها — لكنه لا يثبت «من هو صاحب المفتاح».
 * لذلك يتذكّر هذا السجل أي مفتاح وقّع أي حزمة استوردتها من قبل، ويحذّرك عند حدوث ما يستحق الانتباه:
 *  - مفتاح مختلف وقّع حزمة بنفس الاسم سبق أن استوردتها (احتمال انتحال/تبديل) → [Status.KEY_CHANGED]
 *  - حزمة بنفس الاسم كانت موقّعة سابقاً وجاءت الآن بلا توقيع (توقيع مُجرَّد) → [Status.SIGNATURE_REMOVED]
 *
 * الصنف لا يعتمد على Android: التخزين خلف [Store] (SharedPreferences على الجهاز، خريطة في الذاكرة في الاختبارات).
 */
class RinPakTrust(private val store: Store) {

    interface Store {
        fun get(key: String): String?
        fun put(key: String, value: String)
        fun remove(key: String)
    }

    enum class Status {
        /** بلا توقيع ولا سابقة لهذا الاسم. */
        UNSIGNED,
        /** وقّعها مفتاح هذا الجهاز نفسه (حزمتك أنت). */
        OWN,
        /** مفتاح وضعتَه صراحةً ضمن الناشرين الموثوقين. */
        TRUSTED,
        /** نفس المفتاح الذي وقّع هذه الحزمة في استيراد سابق. */
        KNOWN_SIGNER,
        /** مفتاح لم يسبق رؤيته. */
        NEW_SIGNER,
        /** حزمة بنفس الاسم استوردتَها موقّعة بمفتاح آخر. */
        KEY_CHANGED,
        /** حزمة بنفس الاسم استوردتَها موقّعة، وهذه المرة بلا توقيع. */
        SIGNATURE_REMOVED
    }

    data class Assessment(
        val status: Status,
        val keyId: String?,
        /** المفتاح الذي سُجّل سابقاً لاسم الحزمة (في حالتَي KEY_CHANGED/SIGNATURE_REMOVED). */
        val previousKeyId: String? = null,
        /** تسمية الناشر الموثوق إن وُجدت. */
        val label: String? = null
    ) {
        /** حالات تستحق تحذيراً بارزاً قبل الاستيراد. */
        val isWarning: Boolean get() = status == Status.KEY_CHANGED || status == Status.SIGNATURE_REMOVED
        /** هل يُعرض خيار «ثق بهذا الناشر»؟ */
        val canTrust: Boolean get() = keyId != null && (status == Status.NEW_SIGNER || status == Status.KNOWN_SIGNER || status == Status.KEY_CHANGED)
    }

    fun assess(packageName: String, signerKeyId: String?, ownKeyId: String?): Assessment {
        val previous = store.get(PKG_PREFIX + norm(packageName))?.takeIf { it.isNotEmpty() }
        val key = signerKeyId?.lowercase()
        if (key == null) {
            return if (previous != null) Assessment(Status.SIGNATURE_REMOVED, null, previous)
            else Assessment(Status.UNSIGNED, null)
        }
        if (ownKeyId != null && key == ownKeyId.lowercase()) return Assessment(Status.OWN, key)
        val label = store.get(TRUST_PREFIX + key)
        if (label != null) return Assessment(Status.TRUSTED, key, previous, label.ifEmpty { null })
        if (previous != null && previous != key) return Assessment(Status.KEY_CHANGED, key, previous)
        if (previous == key) return Assessment(Status.KNOWN_SIGNER, key, previous)
        return Assessment(Status.NEW_SIGNER, key)
    }

    /** يُستدعى بعد استيراد ناجح: يربط اسم الحزمة بمفتاح موقّعها (الحزم غير الموقّعة لا تُسجَّل). */
    fun recordImport(packageName: String, signerKeyId: String?) {
        if (signerKeyId != null) store.put(PKG_PREFIX + norm(packageName), signerKeyId.lowercase())
    }

    fun trust(keyId: String, label: String) {
        store.put(TRUST_PREFIX + keyId.lowercase(), label.trim().take(64))
    }

    fun untrust(keyId: String) = store.remove(TRUST_PREFIX + keyId.lowercase())

    fun isTrusted(keyId: String): Boolean = store.get(TRUST_PREFIX + keyId.lowercase()) != null

    private fun norm(name: String) = name.trim().lowercase()

    private companion object {
        const val PKG_PREFIX = "pkg:"
        const val TRUST_PREFIX = "trust:"
    }
}
