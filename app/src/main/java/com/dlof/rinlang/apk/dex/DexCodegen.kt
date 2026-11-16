package com.dlof.rinlang.apk.dex

import com.dlof.rinlang.apk.dex.CodeBuilder.Label
import com.dlof.rinlang.apk.dex.DexBuilder.FieldDef
import com.dlof.rinlang.apk.dex.DexBuilder.FieldRef
import com.dlof.rinlang.apk.dex.DexBuilder.MethodDef
import com.dlof.rinlang.apk.dex.DexBuilder.MethodRef
import com.dlof.rinlang.dexrt.RinNatives

/**
 * مولّد Dalvik bytecode: يحوّل شجرة Rin ([Stmt]) إلى صنف DEX واحد حقيقي:
 *
 *   public final class Lrin/export/Program;
 *     static Object g_<name>;                   // كل متغيّر مُصرَّح على المستوى الأعلى
 *     static Object k<N>;                       // ثوابت رقمية مُصندَقة (Double) تُبنى مرة واحدة في <clinit>
 *     static Object f_<name>(Object…)           // كل دالة Rin  ← دالة static حقيقية
 *     public static void run()                  // كود المستوى الأعلى
 *     static {}                                 // <clinit>: الثوابت + تهيئة المتغيّرات العامة بـ UNDEF
 *
 * نموذج القيم: nil=null، رقم=java.lang.Double، منطقي=java.lang.Boolean، نص=String، مصفوفة=RinArray،
 * قاموس=RinMap. كل العمليات الديناميكية (جمع/مقارنة/فهرسة/طباعة …) تُستدعى من مكتبة التشغيل
 * [com.dlof.rinlang.dexrt] (موجودة أصلاً في RinStudio، فلا تُنسخ داخل dex الناتج) بدلالات المفسّر
 * الأصلي؛ أما بنية البرنامج (دوال، حلقات، شروط، متغيّرات محلية في سجلات Dalvik) فتُصرَّف إلى
 * تعليمات Dalvik حقيقية لا تمرّ بأي مفسّر.
 */
class DexCodegen(
    private val program: List<Stmt>,
    private val classDesc: String = "Lrin/export/Program;",
    private val sourceName: String? = null
) {
    companion object {
        const val RT = "Lcom/dlof/rinlang/dexrt/RinRt;"
        const val OBJ = "Ljava/lang/Object;"
        const val OBJ_ARR = "[Ljava/lang/Object;"
        const val MAIN_METHOD = "run"
        const val MAX_LITERAL_ITEMS = 120

        /** الدوال الأصلية المدعومة وأعداد وسائطها المقبولة (الاسم في RinRt: n_<name>). */
        val NATIVES: Map<String, IntArray> = RinNatives.ARITIES
    }

    private val dex = DexBuilder()
    private val functions = LinkedHashMap<String, FunS>()
    private val globals = LinkedHashSet<String>()
    private val globalFields = HashMap<String, FieldRef>()
    private val numPool = LinkedHashMap<Long, FieldRef>()
    private val methodDefs = ArrayList<MethodDef>()
    private val fnRefs = HashMap<String, MethodRef>()

    private val lineField = dex.field(RT, "line", "I")
    private val undefField = dex.field(RT, "UNDEF", OBJ)
    private val boolTrue = dex.field("Ljava/lang/Boolean;", "TRUE", "Ljava/lang/Boolean;")
    private val boolFalse = dex.field("Ljava/lang/Boolean;", "FALSE", "Ljava/lang/Boolean;")

    private fun rt(name: String, ret: String, vararg params: String): MethodRef = dex.method(RT, name, ret, params.toList())
    private fun rtObj(name: String, n: Int): MethodRef = rt(name, OBJ, *Array(n) { OBJ })

    // ------------------------------------------------------------ نقطة الدخول
    fun generate(): ByteArray {
        analyse()

        // دوال Rin
        for ((name, fn) in functions) {
            val ref = dex.method(classDesc, "f_$name", OBJ, fn.params.map { OBJ })
            fnRefs[name] = ref
        }
        for ((name, fn) in functions) {
            val cb = dex.newCode()
            val ctx = FnCtx(cb, isMain = false, params = fn.params)
            genFunctionBody(ctx, fn)
            methodDefs.add(MethodDef(fnRefs[name]!!, DexBuilder.ACC_PUBLIC or DexBuilder.ACC_STATIC, cb))
        }

        // run()
        run {
            val cb = dex.newCode()
            val ctx = FnCtx(cb, isMain = true, params = emptyList())
            // إعادة تعيين المتغيّرات العامة عند كل تشغيل (زر "إعادة التشغيل" يستدعي run() مرة أخرى)
            if (globals.isNotEmpty()) {
                val t = ctx.alloc()
                cb.sget(t, undefField)
                for (g in globals) cb.sput(t, globalFields[g]!!)
                ctx.sp--
            }
            for (s in program) {
                if (s is FunS) continue
                genStmt(ctx, s, topLevel = true)
            }
            cb.returnVoid()
            methodDefs.add(MethodDef(dex.method(classDesc, MAIN_METHOD, "V", emptyList()),
                DexBuilder.ACC_PUBLIC or DexBuilder.ACC_STATIC, cb))
        }

        // <clinit> (بعد اكتمال مجمّع الثوابت)
        run {
            val cb = dex.newCode()
            val tmp = 0
            val numM = rt("num", OBJ, "D")
            for ((bits, f) in numPool) {
                cb.constWide(tmp, bits)
                cb.invokeStatic(numM, tmp, 2)
                cb.moveResultObject(tmp)
                cb.sput(tmp, f)
            }
            if (globals.isNotEmpty()) {
                cb.sget(tmp, undefField)
                for (g in globals) cb.sput(tmp, globalFields[g]!!)
            }
            cb.reserveRegs(2)
            cb.returnVoid()
            methodDefs.add(MethodDef(dex.method(classDesc, "<clinit>", "V", emptyList()),
                DexBuilder.ACC_STATIC or DexBuilder.ACC_CONSTRUCTOR, cb))
        }

        val sfields = ArrayList<FieldDef>()
        for (g in globals) sfields.add(FieldDef(globalFields[g]!!, DexBuilder.ACC_STATIC))
        for (f in numPool.values) sfields.add(FieldDef(f, DexBuilder.ACC_STATIC))

        dex.addClass(
            classDesc, DexBuilder.ACC_PUBLIC or DexBuilder.ACC_FINAL,
            sourceFile = sourceName, staticFields = sfields, directMethods = methodDefs
        )
        return dex.build()
    }

    // ------------------------------------------------------------ التحليل المسبق
    private fun analyse() {
        for (s in program) {
            when (s) {
                is FunS -> {
                    if (functions.containsKey(s.name)) throw DexUnsupported("الدالة '${s.name}' معرَّفة أكثر من مرة", s.line)
                    functions[s.name] = s
                }
                is LetS -> globals.add(s.name)
                else -> {}
            }
        }
        for (s in program) checkNoNestedFun(s, top = true)
        for (n in functions.keys) {
            if (n in globals) throw DexUnsupported("الاسم '$n' يُستخدم لدالة ولمتغيّر معاً", functions[n]!!.line)
            if (n in NATIVES) throw DexUnsupported("الدالة '$n' تحجب دالة أصلية بنفس الاسم", functions[n]!!.line)
        }
        for (g in globals) globalFields[g] = dex.field(classDesc, "g_$g", OBJ)
    }

    private fun checkNoNestedFun(s: Stmt, top: Boolean) {
        when (s) {
            is FunS -> {
                if (!top) throw DexUnsupported("دالة معرَّفة داخل كتلة/دالة أخرى", s.line)
                s.body.stmts.forEach { checkNoNestedFun(it, false) }
            }
            is BlockS -> s.stmts.forEach { checkNoNestedFun(it, false) }
            is IfS -> { checkNoNestedFun(s.thenB, false); s.elseB?.let { checkNoNestedFun(it, false) } }
            is WhileS -> checkNoNestedFun(s.body, false)
            is ForS -> { s.init?.let { checkNoNestedFun(it, false) }; checkNoNestedFun(s.body, false) }
            else -> {}
        }
    }

    // ------------------------------------------------------------ سياق الدالة
    private class Var(val reg: Int)
    private class LoopCtx(val brk: Label, val cont: Label)

    private inner class FnCtx(val cb: CodeBuilder, val isMain: Boolean, params: List<String>) {
        val scopes = ArrayList<HashMap<String, Var>>()
        var sp = 0
        var maxSp = 0
        val loops = ArrayList<LoopCtx>()
        /** متغيّرات عامة صُرِّحت فعلاً قبل هذه النقطة في كود المستوى الأعلى (قراءتها بلا فحص UNDEF). */
        val definedGlobals = HashSet<String>()
        init {
            cb.paramCount = params.size
            scopes.add(HashMap())
            params.forEachIndexed { i, p -> scopes[0][p] = Var(i) }
            sp = params.size
            maxSp = sp
            cb.reserveRegs(sp)
        }
        fun alloc(): Int { val r = sp++; if (sp > maxSp) maxSp = sp; if (sp > 240) throw DexLimitException("تعبير/دالة معقّدة جداً (سجلات > 240)"); return r }
        fun free(n: Int = 1) { sp -= n }
        fun push() { scopes.add(HashMap()) }
        fun pop() { scopes.removeAt(scopes.size - 1) }
        fun lookup(name: String): Var? { for (i in scopes.indices.reversed()) scopes[i][name]?.let { return it }; return null }
    }

    private fun genFunctionBody(c: FnCtx, fn: FunS) {
        for (s in fn.body.stmts) genStmt(c, s, topLevel = false)
        val t = c.alloc(); c.cb.constInt(t, 0); c.cb.returnObject(t); c.free()
    }

    // ------------------------------------------------------------ العبارات
    private fun setLine(c: FnCtx, line: Int) {
        if (line <= 0) return
        val t = c.alloc()
        c.cb.constInt(t, line)
        c.cb.sput(t, lineField, CodeBuilder.OP_SPUT)
        c.free()
    }

    private fun genStmt(c: FnCtx, s: Stmt, topLevel: Boolean) {
        val cb = c.cb
        when (s) {
            is FunS -> throw DexUnsupported("دالة داخل كتلة", s.line)
            is LetS -> {
                setLine(c, s.line)
                if (topLevel && c.isMain && c.scopes.size == 1) {
                    // متغيّر عام: حقل static
                    val t = c.alloc()
                    if (s.init != null) genExpr(c, s.init, t) else cb.constInt(t, 0)
                    cb.sput(t, globalFields[s.name]!!)
                    c.free()
                    c.definedGlobals.add(s.name)
                } else {
                    val r = c.alloc()          // يبقى محجوزاً حتى نهاية النطاق
                    if (s.init != null) genExpr(c, s.init, r) else cb.constInt(r, 0)
                    c.scopes.last()[s.name] = Var(r)
                }
            }
            is ExprS -> {
                setLine(c, s.line)
                val t = c.alloc(); genExpr(c, s.expr, t); c.free()
            }
            is PrintS -> {
                setLine(c, s.line)
                if (s.exprs.size > MAX_LITERAL_ITEMS) throw DexUnsupported("print بأكثر من $MAX_LITERAL_ITEMS تعبيراً", s.line)
                val base = c.sp
                for (e in s.exprs) { val t = c.alloc(); genExpr(c, e, t) }
                val arr = c.alloc()
                cb.filledNewArray(base, s.exprs.size, dex.type(OBJ_ARR))
                cb.moveResultObject(arr)
                cb.invokeStatic(rt("print", "V", OBJ_ARR), arr, 1)
                c.free(s.exprs.size + 1)
            }
            is BlockS -> {
                c.push(); val saved = c.sp
                for (st in s.stmts) genStmt(c, st, topLevel = false)
                c.sp = saved; c.pop()
            }
            is IfS -> {
                val elseL = cb.label(); val endL = cb.label()
                genCond(c, s.cond, elseL)
                genScopedStmt(c, s.thenB)
                if (s.elseB != null) {
                    cb.gotoLabel(endL)
                    cb.bind(elseL)
                    genScopedStmt(c, s.elseB)
                    cb.bind(endL)
                } else {
                    cb.bind(elseL)
                }
            }
            is WhileS -> {
                val head = cb.label(); val end = cb.label()
                cb.bind(head)
                genCond(c, s.cond, end)
                c.loops.add(LoopCtx(end, head))
                genScopedStmt(c, s.body)
                c.loops.removeAt(c.loops.size - 1)
                cb.gotoLabel(head)
                cb.bind(end)
            }
            is ForS -> {
                c.push(); val saved = c.sp
                if (s.init != null) genStmt(c, s.init, topLevel = false)
                val head = cb.label(); val cont = cb.label(); val end = cb.label()
                cb.bind(head)
                if (s.cond != null) genCond(c, s.cond, end)
                c.loops.add(LoopCtx(end, cont))
                genScopedStmt(c, s.body)
                c.loops.removeAt(c.loops.size - 1)
                cb.bind(cont)
                if (s.incr != null) { setLine(c, s.line); val t = c.alloc(); genExpr(c, s.incr, t); c.free() }
                cb.gotoLabel(head)
                cb.bind(end)
                c.sp = saved; c.pop()
            }
            is ReturnS -> {
                setLine(c, s.line)
                if (c.isMain) {
                    throw DexUnsupported("return خارج دالة", s.line)
                } else {
                    val t = c.alloc()
                    if (s.value != null) genExpr(c, s.value, t) else cb.constInt(t, 0)
                    cb.returnObject(t)
                    c.free()
                }
            }
            is BreakS -> cb.gotoLabel(c.loops.last().brk)
            is ContinueS -> cb.gotoLabel(c.loops.last().cont)
        }
    }

    /** يولّد عبارة كجسم لـ if/while/for: لو لم تكن كتلة تُعامل كنطاق مستقل (let وحيدة داخل if لا تتسرّب). */
    private fun genScopedStmt(c: FnCtx, s: Stmt) {
        if (s is BlockS) { genStmt(c, s, false); return }
        c.push(); val saved = c.sp
        genStmt(c, s, false)
        c.sp = saved; c.pop()
    }

    /** يقفز إلى [falseLabel] إن كان التعبير زائفاً؛ وإلا يتابع. */
    private fun genCond(c: FnCtx, e: Expr, falseLabel: Label) {
        setLine(c, e.line)
        val t = c.alloc()
        genExpr(c, e, t)
        c.cb.invokeStatic(rt("truthy", "Z", OBJ), t, 1)
        c.cb.moveResult(t)
        c.cb.ifEqz(t, falseLabel)
        c.free()
    }

    // ------------------------------------------------------------ التعابير
    private fun numField(v: Double): FieldRef {
        val bits = java.lang.Double.doubleToRawLongBits(v)
        return numPool.getOrPut(bits) { dex.field(classDesc, "k${numPool.size}", OBJ) }
    }

    private fun readGlobal(c: FnCtx, name: String, dst: Int, line: Int) {
        val cb = c.cb
        cb.sget(dst, globalFields[name]!!)
        if (!(c.isMain && name in c.definedGlobals)) {
            // قراءة قد تسبق تنفيذ let (دالة تُنادى مبكراً مثلاً): نفحص UNDEF كما يفعل المفسّر (خطأ متغيّر غير معرَّف).
            val a = c.alloc(); val b = c.alloc()
            cb.move(a, dst); cb.constString(b, dex.str(name))
            cb.invokeStatic(rt("g", OBJ, OBJ, "Ljava/lang/String;"), a, 2)
            cb.moveResultObject(dst)
            c.free(2)
        }
    }

    private fun genExpr(c: FnCtx, e: Expr, dst: Int) {
        val cb = c.cb
        when (e) {
            is NumberE -> cb.sget(dst, numField(e.value))
            is StringE -> cb.constString(dst, dex.str(e.value))
            is BoolE -> cb.sget(dst, if (e.value) boolTrue else boolFalse)
            is NilE -> cb.constInt(dst, 0)
            is VarE -> {
                val v = c.lookup(e.name)
                when {
                    v != null -> cb.move(dst, v.reg)
                    e.name in globals -> readGlobal(c, e.name, dst, e.line)
                    e.name in functions || e.name in NATIVES ->
                        throw DexUnsupported("استخدام دالة '${e.name}' كقيمة", e.line)
                    else -> undefinedVar(c, e.name, e.line, dst)
                }
            }
            is AssignE -> {
                genExpr(c, e.value, dst)
                val v = c.lookup(e.name)
                when {
                    v != null -> cb.move(v.reg, dst)
                    e.name in globals -> {
                        if (!(c.isMain && e.name in c.definedGlobals)) {
                            // الإسناد لمتغيّر لم يُعرَّف بعد خطأ في المفسّر
                            val tmp = c.alloc(); readGlobal(c, e.name, tmp, e.line); c.free()
                        }
                        cb.sput(dst, globalFields[e.name]!!)
                    }
                    else -> undefinedVar(c, e.name, e.line, dst)
                }
            }
            is BinaryE -> {
                val a = c.alloc(); val b = c.alloc()
                genExpr(c, e.left, a)
                genExpr(c, e.right, b)
                val m = when (e.op) {
                    T.PLUS -> "add"; T.MINUS -> "sub"; T.STAR -> "mul"; T.SLASH -> "div"; T.PERCENT -> "mod"
                    T.LESS -> "lt"; T.LESS_EQUAL -> "le"; T.GREATER -> "gt"; T.GREATER_EQUAL -> "ge"
                    T.EQUAL_EQUAL -> "eq"; T.BANG_EQUAL -> "ne"
                    else -> throw DexUnsupported("معامل ثنائي غير مدعوم ${e.op}", e.line)
                }
                setLineIfNeeded(c, e.line)
                cb.invokeStatic(rtObj(m, 2), a, 2)
                cb.moveResultObject(dst)
                c.free(2)
            }
            is UnaryE -> {
                val a = c.alloc()
                genExpr(c, e.right, a)
                setLineIfNeeded(c, e.line)
                cb.invokeStatic(rtObj(if (e.op == T.MINUS) "neg" else "not", 1), a, 1)
                cb.moveResultObject(dst)
                c.free()
            }
            is LogicalE -> {
                genExpr(c, e.left, dst)
                val end = cb.label()
                val t = c.alloc()
                cb.move(t, dst)
                cb.invokeStatic(rt("truthy", "Z", OBJ), t, 1)
                cb.moveResult(t)
                // or: إن كانت اليسرى صادقة ننتهي بها؛ and: إن كانت زائفة ننتهي بها
                if (e.op == T.OR) cb.ifNez(t, end) else cb.ifEqz(t, end)
                c.free()
                genExpr(c, e.right, dst)
                cb.bind(end)
            }
            is CallE -> genCall(c, e, dst)
            is ArrayE -> {
                if (e.elements.size > MAX_LITERAL_ITEMS) throw DexUnsupported("مصفوفة حرفية بأكثر من $MAX_LITERAL_ITEMS عنصراً", e.line)
                if (e.elements.isEmpty()) {
                    cb.invokeStatic(rtObj("mkArr0", 0), 0, 0); cb.moveResultObject(dst)
                } else {
                    val base = c.sp
                    for (x in e.elements) { val t = c.alloc(); genExpr(c, x, t) }
                    val arr = c.alloc()
                    cb.filledNewArray(base, e.elements.size, dex.type(OBJ_ARR))
                    cb.moveResultObject(arr)
                    cb.invokeStatic(rt("mkArr", OBJ, OBJ_ARR), arr, 1)
                    cb.moveResultObject(dst)
                    c.free(e.elements.size + 1)
                }
            }
            is MapE -> {
                if (e.keys.size * 2 > MAX_LITERAL_ITEMS) throw DexUnsupported("قاموس حرفي كبير جداً", e.line)
                if (e.keys.isEmpty()) {
                    cb.invokeStatic(rtObj("mkMap0", 0), 0, 0); cb.moveResultObject(dst)
                } else {
                    val base = c.sp
                    for (i in e.keys.indices) {
                        val k = c.alloc(); genExpr(c, e.keys[i], k)
                        val v = c.alloc(); genExpr(c, e.values[i], v)
                    }
                    val arr = c.alloc()
                    cb.filledNewArray(base, e.keys.size * 2, dex.type(OBJ_ARR))
                    cb.moveResultObject(arr)
                    cb.invokeStatic(rt("mkMap", OBJ, OBJ_ARR), arr, 1)
                    cb.moveResultObject(dst)
                    c.free(e.keys.size * 2 + 1)
                }
            }
            is IndexE -> {
                val a = c.alloc(); val b = c.alloc()
                genExpr(c, e.obj, a); genExpr(c, e.index, b)
                setLineIfNeeded(c, e.line)
                cb.invokeStatic(rtObj("index", 2), a, 2)
                cb.moveResultObject(dst)
                c.free(2)
            }
            is IndexSetE -> {
                val a = c.alloc(); val b = c.alloc(); val v = c.alloc()
                genExpr(c, e.obj, a); genExpr(c, e.index, b); genExpr(c, e.value, v)
                setLineIfNeeded(c, e.line)
                cb.invokeStatic(rtObj("indexSet", 3), a, 3)
                cb.moveResultObject(dst)
                c.free(3)
            }
        }
    }

    /** الأخطاء تُبلَّغ بسطر آخر عبارة بدأت؛ نحدّثه فقط حين يختلف سطر التعبير (متعدد الأسطر). */
    private fun setLineIfNeeded(c: FnCtx, line: Int) { /* السطر يُضبط على مستوى العبارة؛ كافٍ للتشخيص */ }

    private fun undefinedVar(c: FnCtx, name: String, line: Int, dst: Int) {
        val cb = c.cb
        val a = c.alloc()
        cb.constString(a, dex.str(name))
        cb.invokeStatic(rt("undefinedVar", OBJ, "Ljava/lang/String;"), a, 1)
        cb.moveResultObject(dst)
        c.free()
    }

    private fun genCall(c: FnCtx, e: CallE, dst: Int) {
        val cb = c.cb
        val fn = functions[e.callee]
        val nat = if (fn == null) NATIVES[e.callee] else null
        if (c.lookup(e.callee) != null || e.callee in globals)
            throw DexUnsupported("نداء '${e.callee}' وهو متغيّر (دوال كقيم غير مدعومة)", e.line)
        if (fn == null && nat == null) {
            val a = c.alloc()
            cb.constString(a, dex.str(e.callee))
            cb.invokeStatic(rt("undefinedFn", OBJ, "Ljava/lang/String;"), a, 1)
            cb.moveResultObject(dst)
            c.free()
            return
        }
        val expected = fn?.params?.size
        val okArity = if (fn != null) e.args.size == expected else nat!!.contains(e.args.size)
        if (!okArity) {
            val a = c.alloc(); val b = c.alloc(); val d = c.alloc()
            cb.constString(a, dex.str(e.callee))
            cb.constInt(b, e.args.size)
            cb.constInt(d, if (fn != null) -1 else nat!![0])
            cb.invokeStatic(rt("arityError", OBJ, "Ljava/lang/String;", "I", "I"), a, 3)
            cb.moveResultObject(dst)
            c.free(3)
            return
        }
        if (e.args.size > 200) throw DexUnsupported("نداء بأكثر من 200 وسيط", e.line)
        val base = c.sp
        for (a in e.args) { val t = c.alloc(); genExpr(c, a, t) }
        val target: MethodRef = if (fn != null) fnRefs[e.callee]!!
            else dex.method(RT, "n_${e.callee}", OBJ, List(e.args.size) { OBJ })
        cb.invokeStatic(target, base, e.args.size)
        cb.moveResultObject(dst)
        c.free(e.args.size)
    }
}
