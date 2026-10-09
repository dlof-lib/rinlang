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
    val sampleFunctions: String,
    /** أيقونة العرض في شاشة "المكتبات" — افتراضياً شارة "rin+" العامة؛ لبعض المكتبات
     *  أيقونة مخصّصة تعبّر عن فكرتها (مثل movingmask أدناه). */
    val iconRes: Int = R.drawable.ic_rin_stack
)

/**
 * سجل مرجعي (يطابق `embeddedRinLibraries()` في rin_stdlib_libs.h) بالمكتبات القياسية
 * الاثنتين والعشرين المدمجة داخل المفسّر نفسه، وتعمل @import عليها فوراً على أي جهاز دون رفعها.
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
            "lib/inputkit.og.rin", "inputkit",
            "أصناف OOP للتحقق من الإدخال تُمرَّر كـ validator أو في مخطّط النموذج",
            "Required • Length • Range • Integer • OneOf • Matches • Email • Every"
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
        ),
        BuiltinLibraryInfo(
            "lib/langkit.og.rin", "langkit",
            "لبنات جاهزة (tok/tokenNew وتصنيف محارف) لبناء Lexer/Parser/Interpreter للغتك الخاصة",
            "languageInfo • describeLanguage • classifyWord"
        ),
        BuiltinLibraryInfo(
            "lib/astwalk.og.rin", "astwalk",
            "طواف وزيارة شجرة AST (visitor pattern) لمفسّر/مولّد كود لغتك",
            "dispatchTable • visit • visitAll • countNodesDeep • formatAstDeep • formatAstDeepInto"
        ),
        BuiltinLibraryInfo(
            "lib/envkit.og.rin", "envkit",
            "بيئة تنفيذ (Environment / نطاقات متداخلة) لمفسّر لغتك",
            "envNew • envChild • envDefine • envHasOwn • envHas • envGet • envSet • envDepth"
        ),
        BuiltinLibraryInfo(
            "lib/gridkit.og.rin", "gridkit",
            "حلقات متداخلة على شبكات ثنائية الأبعاد (2D grids / matrices)",
            "makeGrid • gridRows • gridCols • gridInBounds • getCell • setCell • forEachCell • mapGrid"
        ),
        BuiltinLibraryInfo(
            "lib/iterkit.og.rin", "iterkit",
            "مكرِّرات (iterators) بنمط hasNext/next فوق المصفوفات",
            "iterNew • iterHasNext • iterPeek • iterNext • iterRemaining • iterReset • iterSkip • iterToArray"
        ),
        BuiltinLibraryInfo(
            "lib/lexkit.og.rin", "lexkit",
            "لبنات محرّك Lexer عام قابل لإعادة الاستخدام لصناعة لغتك",
            "newKeywordTable • classifyWord • newOperatorTable • matchLongestOp • sAtEnd • sPeek • sPeekNext"
        ),
        BuiltinLibraryInfo(
            "lib/loopkit.og.rin", "loopkit",
            "تحكّم عام بالحلقات (loop control primitives) فوق while/for",
            "repeatTimes • countdown • stepLoop • stepLoopCollect • loopUntil • retryUntil • whileCollect"
        ),
        BuiltinLibraryInfo(
            "lib/loopstats.og.rin", "loopstats",
            "تجميع إحصاءات وتقدّم بشكل تدريجي أثناء تنفيذ حلقة",
            "runningStatsNew • runningStatsAdd • runningStatsFromArray • tallyNew • tallyAdd • tallyGet"
        ),
        BuiltinLibraryInfo(
            "lib/parsekit.og.rin", "parsekit",
            "لبنات محلِّل (Parser) بأسلوب أسبقية العمليات (precedence climbing)",
            "precTable • precOf • litNode • identNode • unaryNode • binNode • groupNode • callNode"
        ),
        BuiltinLibraryInfo(
            "lib/runkit.og.rin", "runkit",
            "تشغيل ملفات/أسطر لغتك المخصّصة وبناء تقرير REPL موحّد",
            "runLines • runLinesUntilError • runFile • countSucceeded • countFailed • formatRunReport"
        ),
        BuiltinLibraryInfo(
            "lib/seqkit.og.rin", "seqkit",
            "توليد متتاليات جاهزة كمدخلات لحلقات for/while",
            "rangeStep • linspace • geometricSeq • repeatValue • cycleArr • cycleToLength"
        ),
        BuiltinLibraryInfo(
            "lib/bob.og.rin", "bob",
            "لغة ترميز خفيفة (Markdown-lite) بأسطر بادئة #/>/- ، تُصيَّر إلى HTML أو نص عادي",
            "bobTokenize • bobToHtml • bobToPlain • bobEscapeHtml • bobInfo"
        ),
        BuiltinLibraryInfo(
            "lib/ghpublish.og.rin", "ghpublish",
            "نشر/تحميل مشاريع GitHub حقيقية: دخول بتوكن (ghp_...)، رفع أرشيف zip وفكّ ضغطه ونشره، وتحميل مستودع كاملاً",
            "ghpLogin • ghpPublishProject • ghpUploadZip • ghpDownloadRepo • ghpCreateRepo • ghpRepoInfo"
        ),
        BuiltinLibraryInfo(
            "lib/rinxg.og.rin", "rinxg",
            "لغة تصريحية كاملة (Lexer+Parser+مُصيِّر) لتصميم واجهات الويب فوق Rin، تُترجَم إلى HTML+CSS حقيقي",
            "rxToHtml • rxParseToAst • rxInfo"
        ),
        BuiltinLibraryInfo(
            "lib/movingmask.og.rin", "movingmask",
            "أقنعة متحركة فوق الحاويات والحلقات: فيزياء وحركة (seek/patrol/orbit/سرب/تشكيلات)، آلة حالات، تسلسل JSON، فهرسة مكانية، مؤقتات، FPS وخطوة زمنية ثابتة، أحجام شاشة متجاوبة، أنواع شريط تحميل، لمس وسلاسة حركة، عملات ونقاط، عصا تحكّم وأزرار افتراضية، وقناع منزلق فوق مصفوفات وشبكات، مع تكامل اختياري مع Indsin",
            "mm_new • mm_spawn • mm_tick • mm_flockStep • mm_fsmFire • mm_serialize • mm_setViewport • mm_progressTick • mm_smoothFollow • mm_collectCoinsNear • mm_joystickUpdate • mm_buttonPress",
            iconRes = R.drawable.ic_lib_movingmask
        ),
        BuiltinLibraryInfo(
            "lib/syskit.og.rin", "syskit",
            "عدّة نظام: معلومات المحرّك، مسارات ملفات، ملفات آمنة، إعدادات دائمة (JSON)، سجلّ، وفحوصات",
            "sysInfo • pathJoin • pathNormalize • fileRead • fileReadLines • sysConfigLoad • sysConfigSave • sysLogInfo • sysAssert • sysCheckAll"
        ),
        BuiltinLibraryInfo(
            "lib/requirekit.og.rin", "requirekit",
            "عدّة الحقول والاشتراطات الإلزامية: حقول إلزامية في نموذج/كائن، مجموعات شرطية (أحدها فقط/على الأقل)، واشتراطات عامة تُجمَع أخطاؤها معاً",
            "requireField • requireNonEmptyFields • requireAtLeastOne • requireExactlyOne • requireThat • requireInRange • requireOneOf • requireAll"
        ),
        BuiltinLibraryInfo(
            "lib/physics.og.rin", "physics",
            "مكتبة فيزياء متكاملة: متجهات px*، حركة خطية وإسقاطية، قوى نيوتن، طاقة وزخم واصطدامات، حركة دائرية، نوابض واهتزاز توافقي، وسوائل/طفو",
            "pxVecAdd • pxProjectileRange • pxProjectilePositionAt • pxForceScalar • pxKineticEnergy • pxElasticCollision • pxCentripetalForce • pxSpringPeriod • pxBuoyantForce"
        ),
        BuiltinLibraryInfo(
            "lib/passkit.og.rin", "passkit",
            "Password kit: ready-made policies, strength and entropy analysis, secure generation (password/PIN/passphrase), salted hashing with constant-time verification, lifecycle (history/expiry/lockout), reset tokens and log redaction",
            "pkPolicyStandard • pkCheck • pkAnalyze • pkGenerate • pkGeneratePin • pkPassphrase • pkHash • pkVerify • pkLogin • pkChange • pkResetToken • pkRedact"
        ),
        BuiltinLibraryInfo(
            "lib/passkitlang.og.rin", "passkitlang",
            "The <passkit> tag language as a library: run .passkit files from Rin, register Rin functions callable from .passkit, link .passkit files together (import/run) and link Rin containers",
            "passkitRun • passkitRunSource • passkitGet • passkitRegister • passkitHandlers • passkitUnregister • passkitHandlerNames"
        ),
        BuiltinLibraryInfo(
            "lib/passkitcrypt.og.rin", "passkitcrypt",
            "Encryption and signing for the Passkit family: authenticated Seal/Open, keys (HKDF/PBKDF2/keyring/envelope), signed tokens, URLs and requests, TOTP, recovery codes and Shamir secret sharing, verified against RFC vectors",
            "pcSeal • pcOpen • pcKeyringSeal • pcEnvelopeSeal • pcHkdf • pcPbkdf2 • pcTokenSign • pcTokenVerify • pcTotp • pcShamirSplit • pcMerkleRoot"
        ),
        BuiltinLibraryInfo(
            "lib/passkitdb.og.rin", "passkitdb",
            "Database and container layer for the Passkit family over RCSQL: CRUD, transactions, migrations, encrypted fields and blind index, users, sessions, API keys, hash-chained audit log, rate limit, 2FA and Rin container linking",
            "pdInsert • pdFind • pdInsertEnc • pdFindByBlind • pdUserCreate • pdUserLogin • pdSessionCreate • pdApiKeyCreate • pdAuditLog • pdContSeal"
        )
    )
}
