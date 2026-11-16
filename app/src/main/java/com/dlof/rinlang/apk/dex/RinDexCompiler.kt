package com.dlof.rinlang.apk.dex

/** واجهة مُصرِّف Rin → DEX: مصدر برنامج واحد ← ملف classes.dex حقيقي، أو سبب واضح لعدم القدرة. */
object RinDexCompiler {
    const val PROGRAM_CLASS = "rin.export.Program"
    const val ENTRY_METHOD = DexCodegen.MAIN_METHOD

    sealed class Result {
        class Ok(val dex: ByteArray, val functions: Int) : Result()
        /** البرنامج صالح لكنه يستخدم ما لا يصرَّف إلى DEX ⇒ يتراجع المُصدِّر للمفسّر المُضمَّن. */
        class Unsupported(val reason: String, val line: Int) : Result()
    }

    fun compile(source: String, fileName: String? = null): Result = try {
        val program = DexParser(DexLexer(source).scan()).parse()
        val gen = DexCodegen(program, "L" + PROGRAM_CLASS.replace('.', '/') + ";", fileName)
        Result.Ok(gen.generate(), program.count { it is FunS })
    } catch (e: DexUnsupported) {
        Result.Unsupported(e.message ?: "غير مدعوم", e.line)
    } catch (e: DexLimitException) {
        Result.Unsupported(e.message ?: "تجاوز حدّ بنيوي", 0)
    }
}
