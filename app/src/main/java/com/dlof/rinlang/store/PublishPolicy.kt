package com.dlof.rinlang.store

/**
 * كل "قيود وسياسات النشر" وخوارزمياته في متجر Rin، مجمَّعة في مكان واحد مستقل تماماً عن
 * الواجهة (لا يعتمد على Context أو موارد Android، بنفس أسلوب [VersionUtils] و
 * [PublisherBadgeUtils]) حتى يسهل اختباره وإعادة استخدامه من أي شاشة لاحقاً.
 *
 * يغطي:
 *  - قواعد تسمية الحزمة (طول، أحرف مسموحة، أسماء محجوزة).
 *  - تنسيق رقم الإصدار، وسياسة "الإصدار الجديد يجب أن يكون أحدث من كل ما نُشر سابقاً بهذا الاسم".
 *  - حدود الوصف والتبعيات (عدد أقصى، منع اعتماد الحزمة على نفسها).
 *  - حد أقصى لحجم أرشيف الحزمة الخام (متوافق مع حدود Realtime Database على الخطة المجانية).
 *  - تحديد معدّل النشر (rate limiting) حسب مستوى ثقة الناشر (عادي/موثّق).
 *  - خوارزمية Levenshtein لكشف تشابه الأسماء (حماية من typosquatting لحزم شهيرة).
 *  - خوارزمية اقتراح رقم الإصدار التالي تلقائياً.
 */
object PublishPolicy {

    // ---------------------------------------------------------------------
    // تسمية الحزمة
    // ---------------------------------------------------------------------

    const val MIN_NAME_LENGTH = 3
    const val MAX_NAME_LENGTH = 40

    /**
     * أحرف إنجليزية صغيرة وأرقام و . _ - فقط، يبدأ وينتهي دوماً بحرف أو رقم (لا يبدأ/ينتهي
     * بفاصل)، بطول إجمالي بين [MIN_NAME_LENGTH] و[MAX_NAME_LENGTH] (يُتحقَّق من الطول بشكل
     * منفصل قبل مطابقة النمط حتى تكون رسالة الخطأ أوضح للناشر).
     */
    private val NAME_PATTERN = Regex("^[a-z0-9][a-z0-9._-]*[a-z0-9]$|^[a-z0-9]$")

    /**
     * أسماء محجوزة لا يمكن لأي ناشر أخذها: إما تخصّ المنصّة نفسها أو مصطلحات عامة قد تُستخدم
     * لانتحال صفة رسمية (بعد تطبيع الاسم عبر [normalize]، أي بلا حساسية لحجم الأحرف أو الفواصل).
     */
    val RESERVED_NAMES: Set<String> = setOf(
        "rin", "rinlang", "rinstore", "store", "core", "std", "stdlib",
        "official", "system", "admin", "administrator", "root",
        "null", "undefined", "test", "config", "settings"
    )

    // ---------------------------------------------------------------------
    // الوصف
    // ---------------------------------------------------------------------

    const val MAX_DESCRIPTION_LENGTH = 500

    // ---------------------------------------------------------------------
    // الإصدار
    // ---------------------------------------------------------------------

    /** x.y.z إلزامياً، مع لاحقة اختيارية مثل "-beta" أو "-rc.1". */
    private val VERSION_PATTERN = Regex("^\\d+\\.\\d+\\.\\d+(-[a-zA-Z0-9.]+)?$")

    // ---------------------------------------------------------------------
    // التبعيات
    // ---------------------------------------------------------------------

    const val MAX_DEPENDENCIES = 20

    // ---------------------------------------------------------------------
    // حجم الحزمة
    // ---------------------------------------------------------------------

    /**
     * أقصى حجم لأرشيف الحزمة الخام (zip قبل ترميز base64). تُخزَّن الحزمة كاملة كسلسلة base64
     * واحدة داخل عقدة Realtime Database (خطة Spark المجانية، حد ~10 ميجابايت لكل عقدة)، وترميز
     * base64 يزيد الحجم بنسبة الثلث تقريباً؛ لذا يُحدَّد الأصل الخام بستة ميجابايت لضمان بقاء
     * الناتج المرمَّز (مع بقية حقول الحزمة) ضمن الحد الآمن بفارق كافٍ.
     */
    const val MAX_RAW_PACKAGE_SIZE_BYTES: Long = 6L * 1024 * 1024

    // ---------------------------------------------------------------------
    // تحديد معدّل النشر (Rate limiting)
    // ---------------------------------------------------------------------

    const val RATE_LIMIT_WINDOW_MS: Long = 24L * 60 * 60 * 1000

    /** أقصى عدد عمليات نشر خلال [RATE_LIMIT_WINDOW_MS] لناشر عادي (غير موثّق بعد). */
    const val MAX_PUBLISHES_PER_WINDOW_DEFAULT = 5

    /** أقصى عدد عمليات نشر لناشر موثَّق (راجع [PublisherBadgeUtils]) — حدّ أعلى لكسبه ثقة المتجر. */
    const val MAX_PUBLISHES_PER_WINDOW_VERIFIED = 15

    // ---------------------------------------------------------------------
    // نتيجة التحقق
    // ---------------------------------------------------------------------

    /** نتيجة تطبيق سياسة نشر واحدة: إما مسموحة، أو مرفوضة مع رسالة عربية جاهزة للعرض مباشرة. */
    sealed class PolicyResult {
        object Allowed : PolicyResult()
        data class Denied(val message: String) : PolicyResult()
    }

    /** يطبِّع اسماً للمقارنة: يتجاهل حجم الأحرف والمسافات والفواصل (-, _, .). نفس منطق [PackageRepository]. */
    private fun normalize(name: String): String =
        name.trim().lowercase().replace(Regex("[\\s\\-_.]+"), "")

    // ---------------------------------------------------------------------
    // التحقق من اسم الحزمة
    // ---------------------------------------------------------------------

    fun validateName(name: String): PolicyResult {
        val trimmed = name.trim()
        if (trimmed.length < MIN_NAME_LENGTH || trimmed.length > MAX_NAME_LENGTH) {
            return PolicyResult.Denied(
                "اسم الحزمة يجب أن يكون بين $MIN_NAME_LENGTH و$MAX_NAME_LENGTH حرفاً"
            )
        }
        if (!NAME_PATTERN.matches(trimmed.lowercase())) {
            return PolicyResult.Denied(
                "اسم الحزمة يجب أن يحتوي أحرفاً إنجليزية صغيرة وأرقاماً ونقاطاً وشرطات فقط، " +
                    "ويبدأ وينتهي بحرف أو رقم (لا فراغات ولا رموز أخرى)"
            )
        }
        if (normalize(trimmed) in RESERVED_NAMES) {
            return PolicyResult.Denied("هذا الاسم محجوز لمنصّة Rin نفسها ولا يمكن استخدامه لحزمة")
        }
        return PolicyResult.Allowed
    }

    // ---------------------------------------------------------------------
    // التحقق من الوصف
    // ---------------------------------------------------------------------

    fun validateDescription(description: String): PolicyResult =
        if (description.length > MAX_DESCRIPTION_LENGTH)
            PolicyResult.Denied("الوصف طويل جداً (الحد الأقصى $MAX_DESCRIPTION_LENGTH حرفاً)")
        else PolicyResult.Allowed

    // ---------------------------------------------------------------------
    // التحقق من الإصدار
    // ---------------------------------------------------------------------

    fun validateVersionFormat(version: String): PolicyResult =
        if (!VERSION_PATTERN.matches(version.trim()))
            PolicyResult.Denied("رقم الإصدار يجب أن يكون بصيغة x.y.z، مثل 1.0.0")
        else PolicyResult.Allowed

    /**
     * يمنع نشر إصدار أقدم من أو مساوٍ لأحدث إصدار نُشر سابقاً بنفس الاسم (سواء بواسطة نفس
     * الناشر أو غيره — أسماء المتجر فريدة عموماً، لكن هذه الدالة عامة وتعمل بشكل صحيح أيضاً مع
     * أي تدفّق "نشر إصدار جديد لحزمة موجودة" لاحقاً). قائمة فارغة تعني أن الاسم غير مستخدَم بعد،
     * فأي إصدار مسموح.
     */
    fun validateVersionProgression(newVersion: String, previousVersions: List<String>): PolicyResult {
        if (previousVersions.isEmpty()) return PolicyResult.Allowed
        val latest = previousVersions.maxWithOrNull { a, b -> VersionUtils.compare(a, b) }
            ?: return PolicyResult.Allowed
        return if (VersionUtils.compare(newVersion, latest) <= 0)
            PolicyResult.Denied(
                "رقم الإصدار الجديد ($newVersion) يجب أن يكون أحدث من آخر إصدار منشور بهذا الاسم ($latest)"
            )
        else PolicyResult.Allowed
    }

    /** يقترح رقم الإصدار التالي تلقائياً: زيادة رقم الإصلاح (patch) على أحدث إصدار سابق، أو 1.0.0 إن كان الاسم جديداً. */
    fun suggestNextVersion(previousVersions: List<String>): String {
        val latest = previousVersions.maxWithOrNull { a, b -> VersionUtils.compare(a, b) }
            ?: return "1.0.0"
        val parts = VersionUtils.parse(latest)
        val major = parts.getOrElse(0) { 0 }
        val minor = parts.getOrElse(1) { 0 }
        val patch = parts.getOrElse(2) { 0 }
        return "$major.$minor.${patch + 1}"
    }

    // ---------------------------------------------------------------------
    // التحقق من التبعيات
    // ---------------------------------------------------------------------

    fun validateDependencies(packageName: String, dependencies: Map<String, String>): PolicyResult {
        if (dependencies.size > MAX_DEPENDENCIES) {
            return PolicyResult.Denied("عدد التبعيات يتجاوز الحد الأقصى ($MAX_DEPENDENCIES تبعية)")
        }
        val selfNormalized = normalize(packageName)
        if (dependencies.keys.any { normalize(it) == selfNormalized }) {
            return PolicyResult.Denied("لا يمكن أن تعتمد الحزمة على نفسها كتبعية")
        }
        return PolicyResult.Allowed
    }

    // ---------------------------------------------------------------------
    // التحقق من حجم الحزمة
    // ---------------------------------------------------------------------

    fun validateSize(rawZipSizeBytes: Long): PolicyResult =
        if (rawZipSizeBytes > MAX_RAW_PACKAGE_SIZE_BYTES) {
            val limitMb = MAX_RAW_PACKAGE_SIZE_BYTES / (1024 * 1024)
            PolicyResult.Denied("حجم الحزمة يتجاوز الحد الأقصى المسموح به ($limitMb ميجابايت)")
        } else PolicyResult.Allowed

    // ---------------------------------------------------------------------
    // تحديد معدّل النشر
    // ---------------------------------------------------------------------

    /**
     * يتحقق أن الناشر لم يتجاوز حصّته من عمليات النشر خلال آخر [RATE_LIMIT_WINDOW_MS] — يمنع
     * نشر كميات كبيرة من الحزم دفعة واحدة (سبام أو محاولة حجز أسماء بالجملة). [isVerifiedPublisher]
     * يُستمَد من [PublisherBadgeUtils.isEligible] فيمنح حصّة أعلى للناشرين ذوي السجل الجيد.
     */
    fun checkRateLimit(
        recentPublishTimestamps: List<Long>,
        isVerifiedPublisher: Boolean,
        now: Long = System.currentTimeMillis()
    ): PolicyResult {
        val windowStart = now - RATE_LIMIT_WINDOW_MS
        val countInWindow = recentPublishTimestamps.count { it >= windowStart }
        val limit = if (isVerifiedPublisher) MAX_PUBLISHES_PER_WINDOW_VERIFIED else MAX_PUBLISHES_PER_WINDOW_DEFAULT
        return if (countInWindow >= limit)
            PolicyResult.Denied(
                "لقد وصلت إلى الحد الأقصى من عمليات النشر خلال 24 ساعة ($limit). حاول مرة أخرى لاحقاً"
            )
        else PolicyResult.Allowed
    }

    // ---------------------------------------------------------------------
    // كشف تشابه الأسماء (Typosquatting)
    // ---------------------------------------------------------------------

    /**
     * مسافة Levenshtein (أقل عدد عمليات إضافة/حذف/استبدال حرف واحد لتحويل [a] إلى [b])، بتعقيد
     * O(m×n) في الزمن وO(n) في الذاكرة (سطرين متبادلين بدل مصفوفة كاملة).
     */
    fun levenshtein(a: String, b: String): Int {
        if (a == b) return 0
        if (a.isEmpty()) return b.length
        if (b.isEmpty()) return a.length

        var previousRow = IntArray(b.length + 1) { it }
        var currentRow = IntArray(b.length + 1)

        for (i in 1..a.length) {
            currentRow[0] = i
            for (j in 1..b.length) {
                val substitutionCost = if (a[i - 1] == b[j - 1]) 0 else 1
                currentRow[j] = minOf(
                    currentRow[j - 1] + 1,        // إدراج
                    previousRow[j] + 1,            // حذف
                    previousRow[j - 1] + substitutionCost // استبدال
                )
            }
            val tmp = previousRow
            previousRow = currentRow
            currentRow = tmp
        }
        return previousRow[b.length]
    }

    /**
     * يكشف تشابهاً مريباً بين [candidateName] وأسماء حزم شهيرة موجودة فعلاً (تنزيلات ≥
     * [minDownloadsToProtect])، عندما تكون مسافة Levenshtein بين الاسمين (بعد التطبيع) صغيرة
     * جداً (بين 1 و[maxDistance]) رغم اختلافهما حرفياً — تحذير وقائي يُعرَض للناشر قبل التأكيد
     * النهائي، وليس رفضاً تلقائياً (فقد يكون الاسم مشروعاً تماماً، مثل نسخة رسمية لاحقة).
     */
    fun findSimilarPopularNames(
        candidateName: String,
        allPackages: List<RinPackage>,
        minDownloadsToProtect: Long = 500L,
        maxDistance: Int = 2
    ): List<String> {
        val candidate = normalize(candidateName)
        if (candidate.isEmpty()) return emptyList()
        return allPackages
            .asSequence()
            .filter { it.downloadCount >= minDownloadsToProtect }
            .map { it.name to normalize(it.name) }
            .filter { (_, normalized) -> normalized.isNotEmpty() && normalized != candidate }
            .filter { (_, normalized) -> levenshtein(candidate, normalized) in 1..maxDistance }
            .map { (original, _) -> original }
            .distinct()
            .toList()
    }
}
