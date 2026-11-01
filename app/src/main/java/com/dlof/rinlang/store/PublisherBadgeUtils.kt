package com.dlof.rinlang.store

/**
 * منطق "شارة التوثيق" لناشري متجر Rin — مستقلة تماماً عن [com.dlof.rinlang.auth.RinUser.verified]
 * (الذي يعني فقط أن البريد الإلكتروني تم تأكيده عند إنشاء الحساب). هذه شارة ثقة مختلفة: تُمنح
 * تلقائياً لأي ناشر لديه حزمة واحدة على الأقل تجاوزت حد تنزيلات معيّن *و* حافظت على تقييم جيد،
 * دون أي تدخل يدوي أو حقل إضافي في قاعدة البيانات — تُحسَب محلياً من بيانات الحزم المتوفرة أصلاً.
 */
object PublisherBadgeUtils {

    /** أقل عدد تنزيلات لحزمة واحدة حتى يصبح ناشرها مؤهلاً للشارة. */
    const val MIN_DOWNLOADS = 1000L

    /** أقل متوسط تقييم (من 5) لنفس الحزمة التي تجاوزت حد التنزيلات. */
    const val MIN_RATING = 4.0

    /**
     * يُعتبر الناشر "موثّقاً" إذا كانت لديه حزمة واحدة على الأقل تجمع بين الشرطين معاً:
     * تنزيلات ≥ [MIN_DOWNLOADS] وتقييم متوسط ≥ [MIN_RATING]. لا يكفي تحقيق شرط واحد فقط —
     * حزمة كثيرة التنزيل لكن تقييمها ضعيف لا تمنح الشارة، والعكس صحيح أيضاً.
     */
    fun isEligible(publisherPackages: List<RinPackage>): Boolean =
        publisherPackages.any { it.downloadCount >= MIN_DOWNLOADS && it.averageRating >= MIN_RATING }

    /**
     * يبني مجموعة معرّفات كل الناشرين المؤهَّلين للشارة من قائمة حزم كاملة (مثل كل حزم المتجر)،
     * بمرور واحد فقط بدل استعلام منفصل لكل ناشر — مفيد لشاشة المتجر التي تعرض حزم ناشرين متعددين معاً.
     */
    fun eligiblePublisherUids(allPackages: List<RinPackage>): Set<String> =
        allPackages.groupBy { it.publisherUid }
            .filterValues { isEligible(it) }
            .keys
}
