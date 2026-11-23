package com.dlof.rinlang

/**
 * سمات الطرفية: داكنة وفاتحة. ألوان ANSI المضمّنة مصمَّمة للخلفيات الداكنة، فتُعالَج في السمات الفاتحة
 * عبر [readable] (تعتيم اللون مع الحفاظ على درجته حتى يبلغ التباين الكافي). + مقاييس الحجم/التباعد.
 * منطق خالص بلا أندرويد؛ [RinTerminalView] يطبّق السمة على متغيّراته ويحفظ المعرّف في SharedPreferences.
 */
data class RinTheme(
    val id: String,
    val title: String,
    val bg: Int, val inputBg: Int, val chipBg: Int, val border: Int, val headerBg: Int, val cmdBg: Int,
    val fg: Int, val white: Int, val dim: Int,
    val accent: Int, val blue: Int, val promptGreen: Int, val ok: Int, val error: Int, val warn: Int
) {
    /** سمة فاتحة (خلفيتها ساطعة)؛ تغيّر معالجة ألوان ANSI وطبقات التعتيم. */
    val isLight: Boolean get() = luminance(bg) > LIGHT_THRESHOLD

    /** تدرّج الشعار واللافتات (من، إلى) المناسب لهذه السمة. */
    val gradient: Pair<Int, Int>
        get() = when (id) {
            "midnight" -> 0x7AA2FF to 0x8FD3FF
            "dracula" -> 0xBD93F9 to 0x8BE9FD
            "nord" -> 0x88C0D0 to 0xA3BE8C
            "solarized" -> 0x268BD2 to 0x2AA198
            "amoled" -> 0xA78BFA to 0x7DD3FC
            "matrix" -> 0x1FBF50 to 0x9DFFB8
            "amber" -> 0xFFB000 to 0xFFE08A
            "paper" -> 0x5B3FD1 to 0x0B5FB0
            "daylight" -> 0x6639BA to 0x0550AE
            "solarlight" -> 0x4F54B0 to 0x1F6FB0
            "dawn" -> 0x7A5C9C to 0x286983
            else -> 0x0FA34F to 0x7BF5A8
        }

    companion object {
        private fun c(rgb: Int) = 0xFF000000.toInt() or rgb

        val RIN = RinTheme("rin", "Rin", c(0x0D1117), c(0x161B22), c(0x21262D), c(0x30363D), c(0x010409), c(0x151B23),
            c(0xC9D1D9), c(0xF0F6FC), c(0x6E7681), c(0x8A7CFF), c(0x79C0FF), c(0x56D364), c(0x3FB950), c(0xF85149), c(0xD29922))
        val MIDNIGHT = RinTheme("midnight", "Midnight", c(0x0B1020), c(0x121A30), c(0x1B2542), c(0x2A3760), c(0x060912), c(0x101830),
            c(0xCBD5F5), c(0xF2F5FF), c(0x7482AD), c(0x7AA2FF), c(0x8FD3FF), c(0x6EE7A8), c(0x4ADE80), c(0xFF6B7A), c(0xFBBF24))
        val DRACULA = RinTheme("dracula", "Dracula", c(0x282A36), c(0x2F3243), c(0x3A3D50), c(0x44475A), c(0x1E1F29), c(0x2C2E3C),
            c(0xF8F8F2), c(0xFFFFFF), c(0x8791BC), c(0xBD93F9), c(0x8BE9FD), c(0x50FA7B), c(0x50FA7B), c(0xFF5555), c(0xF1FA8C))
        val NORD = RinTheme("nord", "Nord", c(0x2E3440), c(0x353C4A), c(0x3B4252), c(0x4C566A), c(0x242933), c(0x333A47),
            c(0xD8DEE9), c(0xECEFF4), c(0x8A94A8), c(0x88C0D0), c(0x81A1C1), c(0xA3BE8C), c(0xA3BE8C), c(0xE5848C), c(0xEBCB8B))
        val SOLARIZED = RinTheme("solarized", "Solarized Dark", c(0x002B36), c(0x073642), c(0x0B3F4C), c(0x155664), c(0x00212B), c(0x053541),
            c(0xB6C4C5), c(0xEEE8D5), c(0x6E8A91), c(0x4BA3E3), c(0x2AA198), c(0x859900), c(0x859900), c(0xF0625F), c(0xB58900))
        val AMOLED = RinTheme("amoled", "AMOLED", c(0x000000), c(0x0C0C0C), c(0x181818), c(0x2A2A2A), c(0x000000), c(0x0A0A0A),
            c(0xD0D0D0), c(0xFFFFFF), c(0x7A7A7A), c(0xA78BFA), c(0x7DD3FC), c(0x4ADE80), c(0x4ADE80), c(0xF87171), c(0xFACC15))
        val MATRIX = RinTheme("matrix", "Matrix", c(0x020A04), c(0x06140A), c(0x0C2414), c(0x174A28), c(0x010603), c(0x05100A),
            c(0x9DF2B0), c(0xD6FFE0), c(0x4F8F62), c(0x2BFF6A), c(0x6BFFB0), c(0x39FF7A), c(0x39FF7A), c(0xFF5C5C), c(0xE0F26B))
        val AMBER = RinTheme("amber", "Amber", c(0x120C02), c(0x1C1305), c(0x2A1D08), c(0x4A340F), c(0x0A0601), c(0x181004),
            c(0xFFD28A), c(0xFFF0D0), c(0xA37A3C), c(0xFFB000), c(0xFFCB6B), c(0xB8E060), c(0xB8E060), c(0xFF6B4A), c(0xFFD84A))

        // ---- السمات الفاتحة: `white` هنا = أقوى لون للنص (شبه أسود) ----
        val PAPER = RinTheme("paper", "Paper", c(0xF7F3EA), c(0xFFFDF7), c(0xEAE4D6), c(0xD6CDB8), c(0xEFE9DB), c(0xF0EADC),
            c(0x2B2A27), c(0x111111), c(0x6B6558), c(0x5B3FD1), c(0x0B5FB0), c(0x1E7A34), c(0x1B7F37), c(0xC62828), c(0x8A5A00))
        val DAYLIGHT = RinTheme("daylight", "Daylight", c(0xFFFFFF), c(0xF6F8FA), c(0xEAEEF2), c(0xD0D7DE), c(0xF6F8FA), c(0xF3F5F8),
            c(0x24292F), c(0x0A0C10), c(0x59636E), c(0x6639BA), c(0x0550AE), c(0x116329), c(0x1A7F37), c(0xCF222E), c(0x7D4E00))
        val SOLARLIGHT = RinTheme("solarlight", "Solarized Light", c(0xFDF6E3), c(0xEEE8D5), c(0xE6DFC8), c(0xCFC8AE), c(0xF5EEDA), c(0xF2EBD6),
            c(0x3A4A50), c(0x002B36), c(0x5F757C), c(0x4F54B0), c(0x1F6FB0), c(0x5F7000), c(0x5F7000), c(0xC0322F), c(0x8A6500))
        val DAWN = RinTheme("dawn", "Dawn", c(0xFAF4ED), c(0xFFFAF3), c(0xF2E9E1), c(0xDFD6D2), c(0xF2E9E1), c(0xF4EBE2),
            c(0x463F66), c(0x26233A), c(0x75718F), c(0x7A5C9C), c(0x286983), c(0x3A7655), c(0x3A7655), c(0xB4415F), c(0x9A5A12))

        val ALL = listOf(RIN, MIDNIGHT, DRACULA, NORD, SOLARIZED, AMOLED, MATRIX, AMBER, PAPER, DAYLIGHT, SOLARLIGHT, DAWN)
        val DARK: List<RinTheme> get() = ALL.filter { !it.isLight }
        val LIGHT: List<RinTheme> get() = ALL.filter { it.isLight }

        /** عتبة سطوع الخلفية (luminance نسبي) التي تعدّ السمة فاتحة. */
        const val LIGHT_THRESHOLD = 0.4

        /**
         * يجعل [fg] مقروءاً على [bg]: في الخلفيات الفاتحة فقط، إن قلّ التباين عن [min] يُعتَّم اللون
         * تدريجياً نحو الأسود مع بقاء درجته (hue). الخلفيات الداكنة تُرجَع كما هي. ألفا اللون محفوظة.
         */
        fun readable(fg: Int, bg: Int, min: Double = 4.0): Int {
            if (luminance(bg) <= LIGHT_THRESHOLD || contrast(fg, bg) >= min) return fg
            val a = fg ushr 24
            val r = (fg shr 16) and 255; val g = (fg shr 8) and 255; val b = fg and 255
            var k = 1.0
            var out = fg
            while (k > 0.0) {
                k -= 0.05
                val kk = k.coerceAtLeast(0.0)
                out = (a shl 24) or ((r * kk).toInt() shl 16) or ((g * kk).toInt() shl 8) or (b * kk).toInt()
                if (contrast(out, bg) >= min) break
            }
            return out
        }

        fun byId(id: String?): RinTheme {
            val k = id?.trim()?.lowercase().orEmpty()
            return ALL.firstOrNull { it.id == k } ?: RIN
        }

        /** تطابق دقيق أو بادئة وحيدة (`mid` → midnight)؛ null إن لم يوجد/التبس. */
        fun find(query: String): RinTheme? {
            val q = query.trim().lowercase()
            if (q.isEmpty()) return null
            ALL.firstOrNull { it.id == q }?.let { return it }
            return ALL.filter { it.id.startsWith(q) }.singleOrNull()
        }

        fun luminance(argb: Int): Double {
            fun ch(v: Int): Double { val s = v / 255.0; return if (s <= 0.03928) s / 12.92 else Math.pow((s + 0.055) / 1.055, 2.4) }
            return 0.2126 * ch((argb shr 16) and 255) + 0.7152 * ch((argb shr 8) and 255) + 0.0722 * ch(argb and 255)
        }

        fun contrast(a: Int, b: Int): Double {
            val la = luminance(a); val lb = luminance(b)
            return (maxOf(la, lb) + 0.05) / (minOf(la, lb) + 0.05)
        }
    }
}

/** أحجام جاهزة للخط وتباعد الأسطر. */
object RinSizes {
    val FONT_PRESETS = linkedMapOf("xs" to 10f, "small" to 11.5f, "medium" to 12.5f, "large" to 15f, "xl" to 18f, "xxl" to 21f)
    val SPACING = linkedMapOf("tight" to 0.0f, "normal" to 0.15f, "relaxed" to 0.30f)
    const val DEFAULT_FONT = 12.5f
    const val DEFAULT_SPACING = "normal"
}
