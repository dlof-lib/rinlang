package com.dlof.rinlang.dexrt

/** جدول الدوال الأصلية التي يستطيع مُصرِّف DEX استدعاءها مباشرة: الاسم ← أعداد الوسائط المقبولة (الدالة n_<name> في RinRt). */
object RinNatives {
    val ARITIES: Map<String, IntArray> = mapOf(
        "len" to intArrayOf(1), "upper" to intArrayOf(1), "lower" to intArrayOf(1), "trim" to intArrayOf(1),
        "substr" to intArrayOf(2, 3), "split" to intArrayOf(2), "join" to intArrayOf(2),
        "indexOf" to intArrayOf(2), "replace" to intArrayOf(3), "contains" to intArrayOf(2),
        "charAt" to intArrayOf(2), "toString" to intArrayOf(1), "str" to intArrayOf(1),
        "toNumber" to intArrayOf(1), "toBool" to intArrayOf(1), "isBool" to intArrayOf(1),
        "abs" to intArrayOf(1), "sqrt" to intArrayOf(1), "pow" to intArrayOf(2),
        "floor" to intArrayOf(1), "ceil" to intArrayOf(1), "round" to intArrayOf(1),
        "min" to intArrayOf(1, 2, 3, 4, 5, 6), "max" to intArrayOf(1, 2, 3, 4, 5, 6),
        "push" to intArrayOf(2), "pop" to intArrayOf(1), "sort" to intArrayOf(1),
        "keys" to intArrayOf(1), "values" to intArrayOf(1), "has" to intArrayOf(2), "remove" to intArrayOf(2),
        "sum" to intArrayOf(1), "mean" to intArrayOf(1), "maxOf" to intArrayOf(1), "minOf" to intArrayOf(1),
        "chr" to intArrayOf(1), "ord" to intArrayOf(1)
    )
}
