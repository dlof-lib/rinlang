package com.dlof.rinlang

/** حسابات القصّ (center-crop) الخالصة — تُختبر بلا أندرويد. */
object CropMath {
    /** مستطيل المصدر (l,t,r,b) داخل صورة bw×bh ليملأ هدفاً w×h بنفس النسبة. */
    fun centerCrop(bw: Int, bh: Int, w: Int, h: Int): IntArray {
        if (bw <= 0 || bh <= 0 || w <= 0 || h <= 0) return intArrayOf(0, 0, maxOf(bw, 0), maxOf(bh, 0))
        val srcRatio = bw.toFloat() / bh
        val dstRatio = w.toFloat() / h
        return if (srcRatio > dstRatio) {
            val sw = (bh * dstRatio).toInt().coerceIn(1, bw)
            val l = (bw - sw) / 2
            intArrayOf(l, 0, l + sw, bh)
        } else {
            val sh = (bw / dstRatio).toInt().coerceIn(1, bh)
            val t = (bh - sh) / 2
            intArrayOf(0, t, bw, t + sh)
        }
    }

    /** أصغر inSampleSize (قوة 2) يجعل أطول ضلع ≤ [maxSide]. */
    fun sampleSize(w: Int, h: Int, maxSide: Int): Int {
        var s = 1
        var m = maxOf(w, h)
        while (m / 2 >= maxSide) { s *= 2; m /= 2 }
        return s
    }

    /** تحجيم «تغطية» لصورة iw×ih داخل w×h: [scale, dx, dy] بحيث تملأ الهدف وتتمركز. */
    fun coverTransform(iw: Int, ih: Int, w: Int, h: Int): FloatArray {
        if (iw <= 0 || ih <= 0 || w <= 0 || h <= 0) return floatArrayOf(1f, 0f, 0f)
        val sc = maxOf(w.toFloat() / iw, h.toFloat() / ih)
        return floatArrayOf(sc, (w - iw * sc) / 2f, (h - ih * sc) / 2f)
    }
}
