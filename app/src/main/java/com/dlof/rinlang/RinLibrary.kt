package com.dlof.rinlang

import java.io.File

/**
 * مكتبة Rin تابعة لمستخدم: ملف `.og.rin` حقيقي داخل مجلد `lib/` الخاص بمشروع ما
 * (انظر [ProjectManager] لعمليات القراءة/الكتابة/الرفع/الحذف عليها).
 *
 * تخزَّن هذه المكتبات فعلياً على القرص داخل `<project>/lib/<name>.og.rin`، أي أسفل
 * basePath الذي يُمرَّر للمحرّك C++ (انظر [RinEngine]) — لذا فإن عبارة
 * `@import "lib/<name>.og.rin";` بداخل كود المشروع تجدها وتستوردها مباشرة دون أي
 * إعداد إضافي (نفس آلية container.import، انظر rin_interpreter.cpp).
 */
data class RinLibrary(
    val name: String,
    val file: File,
    val sizeBytes: Long,
    val lastModified: Long
)

/** وصف ثابت لمكتبة مدمجة (embedded) داخل محرّك C++ نفسه (rin_stdlib_libs.h)، لعرضها في قسم "المكتبات". */
data class BuiltinLibraryInfo(
    /** الاسم الكامل كما يُستورد به، مثل "lib/math.og.rin". */
    val importPath: String,
    /** اسم مختصر للعرض، مثل "math". */
    val displayName: String,
    /** وصف قصير بالعربية لما تحتويه المكتبة. */
    val description: String,
    /** أبرز الدوال المتاحة، للعرض السريع دون الحاجة لفتح الشيفرة المصدرية. */
    val sampleFunctions: String
)

/**
 * سجل مرجعي (يطابق `embeddedRinLibraries()` في rin_stdlib_libs.h) بالمكتبات القياسية
 * السبع المدمجة داخل المفسّر نفسه، وتعمل @import عليها فوراً على أي جهاز دون رفعها.
 * هذا السجل نصي فقط (للعرض والإدراج السريع في المحرر)، ولا يكرر شيفرة المكتبات نفسها.
 */
object BuiltinLibraries {
    val all: List<BuiltinLibraryInfo> = listOf(
        BuiltinLibraryInfo(
            "lib/math.og.rin", "math",
            "امتدادات رياضية فوق stdlib الأساسية",
            "factorial • gcd • lcm • isPrime • clamp • lerp • sign"
        ),
        BuiltinLibraryInfo(
            "lib/strings.og.rin", "strings",
            "دوال نصوص إضافية",
            "capitalize • reverseStr • startsWith • padLeft • titleCase"
        ),
        BuiltinLibraryInfo(
            "lib/data.og.rin", "data",
            "أدوات مصفوفات وقواميس (arrays/maps)",
            "range • unique • chunk • zip • first • last • mapGet • mapMerge"
        ),
        BuiltinLibraryInfo(
            "lib/validate.og.rin", "validate",
            "دوال تحقّق (validation) آمنة لا ترمي أخطاء أبداً",
            "isEmpty • isNumeric • isEmail • isInRange • isStrongPassword"
        ),
        BuiltinLibraryInfo(
            "lib/functional.og.rin", "functional",
            "دوال ترتيبية عليا (map/filter/reduce) على المصفوفات",
            "mapArr • filterArr • reduceArr • forEachArr • findArr • composeApply"
        ),
        BuiltinLibraryInfo(
            "lib/oglang.og.rin", "oglang",
            "صناعة حزم .og.rin ومحرّك لغات مصغّرة (mini-languages) فوق Rin",
            "pkgInfo • describePkg • rule • langNew • runLine • runProgram"
        ),
        BuiltinLibraryInfo(
            "lib/ringo.og.rin", "ringo",
            "لغة ترميز خفيفة بوسوم [tag] (Ringo)، تُصيَّر إلى HTML أو نص عادي",
            "ringoToHtml • ringoToPlain • ringoTokenize • ringoInfo"
        )
    )
}
