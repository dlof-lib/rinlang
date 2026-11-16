package com.dlof.rinlang.apk.dex

import com.dlof.rinlang.apk.dex.DexBuilder.FieldRef
import com.dlof.rinlang.apk.dex.DexBuilder.MethodRef
import com.dlof.rinlang.apk.dex.DexBuilder.StrRef
import com.dlof.rinlang.apk.dex.DexBuilder.TypeRef

/** تُرمى حين يتجاوز المولّد حدّاً بنيوياً لصيغة Dalvik (عدد السجلات مثلاً) — يتراجع المصدِّر للمفسّر. */
class DexLimitException(message: String) : RuntimeException(message)

/**
 * مجمِّع Dalvik: يبني متن دالة واحد كقائمة تعليمات رمزية، ثم يحسم عند [writeCodeItem]:
 *  1) ترقيم السجلات الفعلي (المعاملات في أعلى السجلات كما تفرض Dalvik، لذا يستخدم المولّد أرقاماً
 *     "افتراضية" تبدأ المعاملات منها من 0 ثم تُحوَّل هنا).
 *  2) اختيار أضيق صيغة ترميز ممكنة لكل تعليمة (const/4 ← const/16 ← const، move-object ← /from16 ← /16 …).
 *  3) إرخاء القفزات (relaxation): كل قفزة تبدأ قصيرة (goto/16 أو if-*z ±32K) وتُرقَّى إلى goto/32 (أو
 *     "if معكوس + goto/32") فقط إن خرج هدفها عن المدى، حتى الاستقرار.
 *
 * كل التعليمات المُولَّدة من الصيغ الكلاسيكية لـ Dalvik (opcodes من dalvik-bytecode في AOSP).
 */
class CodeBuilder internal constructor(private val dex: DexBuilder) {

    class Label { internal var pos = -1 }

    /** عدد المعاملات (سجلات ins) — تأخذ الأرقام الافتراضية 0 until paramCount. */
    var paramCount = 0
    private var maxVReg = -1
    private var outs = 0
    private val insns = ArrayList<Insn>()

    private fun use(v: Int) { if (v > maxVReg) maxVReg = v }
    fun reserveRegs(count: Int) { use(count - 1) }
    private fun add(i: Insn) { insns.add(i) }

    // ---------------------------------------------------------------- واجهة التوليد
    fun label() = Label()
    fun bind(l: Label) { add(Insn.Bind(l)) }
    fun move(dst: Int, src: Int) { use(dst); use(src); add(Insn.Move(dst, src)) }
    fun constInt(dst: Int, value: Int) { use(dst); add(Insn.ConstInt(dst, value)) }
    fun constWide(dst: Int, bits: Long) { use(dst + 1); add(Insn.ConstWide(dst, bits)) }
    fun constString(dst: Int, s: StrRef) { use(dst); add(Insn.ConstString(dst, s)) }
    fun sget(dst: Int, f: FieldRef, op: Int = OP_SGET_OBJECT) { use(dst); add(Insn.SField(op, dst, f)) }
    fun sput(src: Int, f: FieldRef, op: Int = OP_SPUT_OBJECT) { use(src); add(Insn.SField(op, src, f)) }
    fun invokeStatic(m: MethodRef, firstReg: Int, regCount: Int) {
        if (regCount > 0) use(firstReg + regCount - 1)
        if (regCount > outs) outs = regCount
        add(Insn.Invoke(m, firstReg, regCount))
    }
    fun moveResultObject(dst: Int) { use(dst); add(Insn.Unary(OP_MOVE_RESULT_OBJECT, dst)) }
    fun moveResult(dst: Int) { use(dst); add(Insn.Unary(OP_MOVE_RESULT, dst)) }
    fun returnObject(src: Int) { use(src); add(Insn.Unary(OP_RETURN_OBJECT, src)) }
    fun returnVoid() { add(Insn.Nullary(OP_RETURN_VOID)) }
    fun filledNewArray(first: Int, count: Int, arrayType: TypeRef) {
        if (count > 0) use(first + count - 1)
        if (count > outs) outs = count
        add(Insn.FilledNewArrayRange(first, count, arrayType))
    }
    fun gotoLabel(l: Label) { add(Insn.Goto(l)) }
    /** قفز إن كان السجل == 0 / != 0 (للمنطقي int أو المرجع null). */
    fun ifEqz(reg: Int, l: Label) { use(reg); add(Insn.IfZ(OP_IF_EQZ, reg, l)) }
    fun ifNez(reg: Int, l: Label) { use(reg); add(Insn.IfZ(OP_IF_NEZ, reg, l)) }

    val virtualRegisterCount: Int get() = maxVReg + 1

    // ---------------------------------------------------------------- التجميع
    internal class Ctx(val regs: Int, val ins: Int) {
        fun phys(v: Int): Int = if (v < ins) regs - ins + v else v - ins
    }

    private var assembled: ShortArray? = null
    private var regsSize = 0

    internal fun writeCodeItem(out: DexBuilder.Buf) {
        val regs = maxOf(virtualRegisterCount, paramCount)
        if (regs > 255) throw DexLimitException("الدالة تحتاج $regs سجلاً (الحد 255 لتعليمات 8-بت)")
        val ctx = Ctx(regs, paramCount)
        val code = assemble(ctx)
        out.u2(regs)
        out.u2(paramCount)
        out.u2(outs)
        out.u2(0)                 // tries_size
        out.u4(0)                 // debug_info_off
        out.u4(code.size)
        for (u in code) out.u2(u.toInt())
        // لا حشو إضافي: لا يوجد try/catch وبالتالي لا يُشترط محاذاة زوجية لـ insns.
    }

    private fun assemble(ctx: Ctx): ShortArray {
        // إرخاء القفزات
        val branchy = insns.filter { it is Insn.Branch }.map { it as Insn.Branch }
        branchy.forEach { it.long = false }
        var guard = 0
        while (true) {
            layout(ctx)
            var changed = false
            for (b in branchy) {
                if (!b.long && !b.fits()) { b.long = true; changed = true }
            }
            if (!changed) break
            if (++guard > 64) throw DexLimitException("تعذّر استقرار إرخاء القفزات")
        }
        val units = ArrayList<Int>(insns.size * 3)
        var pc = 0
        for (i in insns) {
            i.pc = pc
            i.emit(ctx, units)
            pc += i.size(ctx)
            check(units.size == pc) { "حجم التعليمة ${i::class.simpleName} لا يطابق ما كُتب (${units.size} != $pc)" }
        }
        return ShortArray(units.size) { units[it].toShort() }
    }

    private fun layout(ctx: Ctx) {
        var pc = 0
        for (i in insns) {
            i.pc = pc
            if (i is Insn.Bind) i.label.pos = pc
            pc += i.size(ctx)
        }
    }

    // ---------------------------------------------------------------- التعليمات
    internal sealed class Insn {
        var pc = 0
        abstract fun size(c: Ctx): Int
        abstract fun emit(c: Ctx, out: MutableList<Int>)

        class Bind(val label: Label) : Insn() {
            override fun size(c: Ctx) = 0
            override fun emit(c: Ctx, out: MutableList<Int>) {}
        }

        class Nullary(val op: Int) : Insn() {
            override fun size(c: Ctx) = 1
            override fun emit(c: Ctx, out: MutableList<Int>) { out.add(op) }
        }

        /** 11x: AA|op */
        class Unary(val op: Int, val a: Int) : Insn() {
            override fun size(c: Ctx) = 1
            override fun emit(c: Ctx, out: MutableList<Int>) {
                val pa = c.phys(a); chk8(pa)
                out.add(op or (pa shl 8))
            }
        }

        class Move(val dst: Int, val src: Int) : Insn() {
            override fun size(c: Ctx): Int {
                val d = c.phys(dst); val s = c.phys(src)
                return if (d < 16 && s < 16) 1 else if (d < 256) 2 else 3
            }
            override fun emit(c: Ctx, out: MutableList<Int>) {
                val d = c.phys(dst); val s = c.phys(src)
                when {
                    d < 16 && s < 16 -> out.add(0x07 or (d shl 8) or (s shl 12))        // move-object 12x
                    d < 256 -> { out.add(0x08 or (d shl 8)); out.add(s) }                // move-object/from16 22x
                    else -> { out.add(0x09); out.add(d); out.add(s) }                    // move-object/16 32x
                }
            }
        }

        class ConstInt(val dst: Int, val value: Int) : Insn() {
            override fun size(c: Ctx): Int {
                val d = c.phys(dst)
                return if (value in -8..7 && d < 16) 1 else if (value in -32768..32767) 2 else 3
            }
            override fun emit(c: Ctx, out: MutableList<Int>) {
                val d = c.phys(dst); chk8(d)
                when {
                    value in -8..7 && d < 16 -> out.add(0x12 or (d shl 8) or ((value and 0xf) shl 12))
                    value in -32768..32767 -> { out.add(0x13 or (d shl 8)); out.add(value and 0xffff) }
                    else -> { out.add(0x14 or (d shl 8)); out.add(value and 0xffff); out.add((value ushr 16) and 0xffff) }
                }
            }
        }

        class ConstWide(val dst: Int, val bits: Long) : Insn() {
            override fun size(c: Ctx) = 5
            override fun emit(c: Ctx, out: MutableList<Int>) {
                val d = c.phys(dst); chk8(d)
                out.add(0x18 or (d shl 8))
                out.add((bits and 0xffff).toInt()); out.add(((bits ushr 16) and 0xffff).toInt())
                out.add(((bits ushr 32) and 0xffff).toInt()); out.add(((bits ushr 48) and 0xffff).toInt())
            }
        }

        class ConstString(val dst: Int, val s: StrRef) : Insn() {
            override fun size(c: Ctx) = if (s.idx > 0xffff) 3 else 2
            override fun emit(c: Ctx, out: MutableList<Int>) {
                val d = c.phys(dst); chk8(d)
                if (s.idx > 0xffff) { out.add(0x1b or (d shl 8)); out.add(s.idx and 0xffff); out.add((s.idx ushr 16) and 0xffff) }
                else { out.add(0x1a or (d shl 8)); out.add(s.idx) }
            }
        }

        class SField(val op: Int, val reg: Int, val f: FieldRef) : Insn() {
            override fun size(c: Ctx) = 2
            override fun emit(c: Ctx, out: MutableList<Int>) {
                val r = c.phys(reg); chk8(r)
                out.add(op or (r shl 8)); out.add(f.idx)
            }
        }

        class Invoke(val m: MethodRef, val first: Int, val count: Int) : Insn() {
            private fun useShort(c: Ctx): Boolean {
                if (count == 0) return true
                val f = c.phys(first)
                return count <= 5 && f + count - 1 <= 15
            }
            override fun size(c: Ctx) = 3
            override fun emit(c: Ctx, out: MutableList<Int>) {
                if (useShort(c)) {
                    val f = if (count == 0) 0 else c.phys(first)
                    var word = 0
                    for (k in 0 until minOf(count, 4)) word = word or (((f + k) and 0xf) shl (4 * k))
                    val g = if (count == 5) (f + 4) and 0xf else 0
                    out.add(0x71 or (g shl 8) or (count shl 12))                       // invoke-static 35c
                    out.add(m.idx)
                    out.add(word)
                } else {
                    out.add(0x77 or (count shl 8))                                      // invoke-static/range 3rc
                    out.add(m.idx); out.add(c.phys(first))
                }
            }
        }

        /** filled-new-array/range {vFirst..}, type — المصفوفة تُسلَّم بعده عبر move-result-object. */
        class FilledNewArrayRange(val first: Int, val count: Int, val type: TypeRef) : Insn() {
            override fun size(c: Ctx) = 3
            override fun emit(c: Ctx, out: MutableList<Int>) {
                if (count > 255) throw DexLimitException("عدد عناصر filled-new-array/range > 255")
                out.add(0x25 or (count shl 8)); out.add(type.idx); out.add(if (count == 0) 0 else c.phys(first))
            }
        }

        /** تعليمات متفرعة قابلة للإرخاء. */
        sealed class Branch(val target: Label) : Insn() {
            var long = false
            abstract fun fits(): Boolean
            protected fun off(): Int = target.pos - pc
        }

        class Goto(target: Label) : Branch(target) {
            override fun size(c: Ctx) = if (long) 3 else 2
            override fun fits() = off() in -32768..32767
            override fun emit(c: Ctx, out: MutableList<Int>) {
                val o = off()
                if (!long) { out.add(0x29); out.add(o and 0xffff) }                      // goto/16 20t
                else { out.add(0x2a); out.add(o and 0xffff); out.add((o ushr 16) and 0xffff) } // goto/32 30t
            }
        }

        class IfZ(val op: Int, val reg: Int, target: Label) : Branch(target) {
            override fun size(c: Ctx) = if (long) 5 else 2
            override fun fits() = off() in -32768..32767
            override fun emit(c: Ctx, out: MutableList<Int>) {
                val r = c.phys(reg); chk8(r)
                if (!long) { out.add(op or (r shl 8)); out.add(off() and 0xffff) }
                else {
                    val inv = if (op == OP_IF_EQZ) OP_IF_NEZ else OP_IF_EQZ
                    out.add(inv or (r shl 8)); out.add(5)                                 // يتخطى goto/32 التالية
                    val o = target.pos - (pc + 2)
                    out.add(0x2a); out.add(o and 0xffff); out.add((o ushr 16) and 0xffff)
                }
            }
        }

        companion object {
            fun chk8(r: Int) { if (r > 255) throw DexLimitException("سجل فعلي $r يتجاوز 255") }
        }
    }

    companion object {
        const val OP_RETURN_VOID = 0x0e
        const val OP_RETURN_OBJECT = 0x11
        const val OP_MOVE_RESULT = 0x0a
        const val OP_MOVE_RESULT_OBJECT = 0x0c
        const val OP_IF_EQZ = 0x38
        const val OP_IF_NEZ = 0x39
        const val OP_SGET = 0x60
        const val OP_SGET_OBJECT = 0x62
        const val OP_SPUT = 0x67
        const val OP_SPUT_OBJECT = 0x69
    }
}
