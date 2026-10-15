package com.dlof.rinlang

/**
 * غلاف Kotlin رقيق حول محرك المحرر الأصلي المكتوب من الصفر بلغة C++
 * (app/src/main/cpp/editor/rin_editor_engine.h/.cpp، معروض عبر app/src/main/cpp/editor/rin_editor_jni.cpp).
 *
 * كل التحرير الفعلي — التخزين، التراجع/الإعادة، الإزاحة التلقائية، إغلاق الأقواس/التنصيص
 * التلقائي، أوامر السطر، البحث، والتلوين النحوي عبر rin::Lexer الحقيقي — يحدث داخل C++.
 * هذا الصنف مسؤول فقط عن: (1) دورة حياة المقبض الأصلي (native handle)، و(2) تحويل الإحداثيات
 * بين "إزاحة بايت UTF-8" (ما يستخدمه المحرك C++، طبقًا لاصطلاح rin::Lexer) و"فهرس حرف UTF-16"
 * (ما تستخدمه كل واجهات برمجة النصوص في Kotlin/Android: String, Canvas.measureText, ...).
 *
 * ملاحظة إصلاح مهمة: كل دالة تُرسل أو تستقبل "عمودًا" من/إلى الجانب الأصلي (C++) يجب أن تمرّ عبر
 * [charIndexToByteOffset]/[byteOffsetToCharIndex] هنا — وإلا فإن أي سطر يحوي حرفًا خارج ASCII
 * (كالعربية، وهي أساسية في هذا التطبيق) يجعل "عمود-حرف UTF-16" و"إزاحة-بايت UTF-8" يفترقان،
 * فيهبط المؤشر/التحديد في منتصف حرف متعدد البايتات. أسوأ نتيجة لذلك: JNI's NewStringUTF على
 * سلسلة UTF-8 غير صالحة (مقطوعة من المنتصف) يُنهي العملية بالكامل — وهو بالضبط سبب "الخروج
 * عند كتابة كود" عند وجود نص عربي (تعليق أو سلسلة نصية) في المستند. كل دالة عامة أدناه تضمن
 * التحويل تلقائيًا حتى لا يضطر المستدعي (RinCodeEditorView وغيره) لمعرفة هذا التفصيل إطلاقًا.
 *
 * مثيل واحد من هذا الصنف = مستند واحد مفتوح في المحرر.
 */
class RinNativeEditor {

    private var handle: Long = nativeCreate()
    private var destroyed = false

    companion object {
        init {
            // نفس مكتبة rinengine.so التي يحمّلها RinEngine.kt؛ استدعاء System.loadLibrary
            // لنفس المكتبة أكثر من مرة من نفس ClassLoader آمن تمامًا على أندرويد (لا تأثير في
            // المرات اللاحقة)، فلا حاجة لتنسيق أيّهما يُحمَّل أولاً بين هذا الصنف و[RinEngine].
            System.loadLibrary("rinengine")
        }

        @JvmStatic private external fun nativeCreate(): Long
        @JvmStatic private external fun nativeDestroy(handle: Long)
        @JvmStatic private external fun nativeSetText(handle: Long, text: String)
        @JvmStatic private external fun nativeGetText(handle: Long): String
        @JvmStatic private external fun nativeSetLanguage(handle: Long, extension: String)
        @JvmStatic private external fun nativeGetLineCount(handle: Long): Int
        @JvmStatic private external fun nativeGetLine(handle: Long, line: Int): String
        @JvmStatic private external fun nativeGetCursor(handle: Long): IntArray
        @JvmStatic private external fun nativeGetSelection(handle: Long): IntArray
        @JvmStatic private external fun nativeSetCursor(handle: Long, line: Int, col: Int, extend: Boolean)
        @JvmStatic private external fun nativeSetSelection(handle: Long, aLine: Int, aCol: Int, bLine: Int, bCol: Int)
        @JvmStatic private external fun nativeCollapseSelection(handle: Long)
        @JvmStatic private external fun nativeInsertText(handle: Long, text: String, smart: Boolean): IntArray
        @JvmStatic private external fun nativeDeleteBackward(handle: Long): IntArray
        @JvmStatic private external fun nativeDeleteForward(handle: Long): IntArray
        @JvmStatic private external fun nativeReplaceRange(handle: Long, sl: Int, sc: Int, el: Int, ec: Int, text: String): IntArray
        @JvmStatic private external fun nativeUndo(handle: Long): IntArray?
        @JvmStatic private external fun nativeRedo(handle: Long): IntArray?
        @JvmStatic private external fun nativeDuplicateLine(handle: Long): IntArray
        @JvmStatic private external fun nativeDeleteLine(handle: Long): IntArray
        @JvmStatic private external fun nativeMoveLineUp(handle: Long): IntArray
        @JvmStatic private external fun nativeMoveLineDown(handle: Long): IntArray
        @JvmStatic private external fun nativeToggleComment(handle: Long): IntArray
        @JvmStatic private external fun nativeIndentSelection(handle: Long): IntArray
        @JvmStatic private external fun nativeUnindentSelection(handle: Long): IntArray
        @JvmStatic private external fun nativeCheckBracketBalance(handle: Long): Int
        @JvmStatic private external fun nativeGetHighlightSpansFlat(handle: Long): IntArray
        @JvmStatic private external fun nativeFindAllFlat(handle: Long, query: String, caseSensitive: Boolean): IntArray
        @JvmStatic private external fun nativeGetSuggestions(handle: Long, prefix: String, maxResults: Int): Array<String>
        @JvmStatic private external fun nativeLineStartPosition(handle: Long, oneBasedLine: Int): IntArray
        @JvmStatic private external fun nativeGetDiagnosticsPacked(handle: Long): Array<String>
        @JvmStatic private external fun nativeGetFoldRangesFlat(handle: Long): IntArray
        @JvmStatic private external fun nativeGetOutlinePacked(handle: Long): Array<String>
        @JvmStatic private external fun nativeReindentDocument(handle: Long, width: Int): Boolean
        @JvmStatic private external fun nativeExpandSnippet(handle: Long): IntArray?
        @JvmStatic private external fun nativeComplete(handle: Long, explicitInvoke: Boolean, maxResults: Int): Array<String>?
        @JvmStatic private external fun nativeSignatureHelp(handle: Long): String?
        @JvmStatic private external fun nativeDescribe(handle: Long, line: Int, byteCol: Int): String?
        @JvmStatic private external fun nativeRecordCompletionUse(label: String)
        @JvmStatic private external fun nativeIsSnippetTrigger(word: String): Boolean
        @JvmStatic private external fun nativeSnippetTitle(word: String): String

        /** هل [word] اسم قالب (snippet) مثل each/repeat/retry/loop؟ */
        fun isSnippetTrigger(word: String): Boolean = nativeIsSnippetTrigger(word)
        /** وصف عربي قصير للقالب، أو "" إن لم يكن قالباً. */
        fun snippetTitle(word: String): String = nativeSnippetTitle(word)

        /** يحوّل فهرس-حرف UTF-16 (Kotlin/Java) داخل [line] إلى إزاحة-بايت UTF-8 (المحرك C++). */
        fun charIndexToByteOffset(line: String, charIndex: Int): Int {
            val clamped = charIndex.coerceIn(0, line.length)
            return line.substring(0, clamped).toByteArray(Charsets.UTF_8).size
        }

        /** يحوّل إزاحة-بايت UTF-8 (المحرك C++) داخل [line] إلى فهرس-حرف UTF-16 (Kotlin/Java) — O(n). */
        fun byteOffsetToCharIndex(line: String, byteOffset: Int): Int {
            if (byteOffset <= 0) return 0
            var bytes = 0
            var i = 0
            val n = line.length
            while (i < n) {
                if (bytes >= byteOffset) return i
                val cp = line.codePointAt(i)
                val charCount = Character.charCount(cp)
                bytes += String(Character.toChars(cp)).toByteArray(Charsets.UTF_8).size
                i += charCount
            }
            return n
        }
    }

    data class Pos(val line: Int, val col: Int)
    data class Selection(val hasSelection: Boolean, val start: Pos, val end: Pos)
    data class Highlight(val line: Int, val startCol: Int, val endCol: Int, val kind: Int)
    data class FindMatch(val line: Int, val startCol: Int, val endCol: Int)

    /** تشخيص صياغة حي واحد (خطأ/تحذير) — انظر [DiagnosticSeverity]. الأعمدة فهرس-حرف UTF-16 جاهز للرسم مباشرة. */
    data class Diagnostic(
        val line: Int,
        val startCol: Int,
        val endCol: Int,
        val severity: Int,
        val code: String,
        val message: String
    )

    /** يطابق rinedit::DiagnosticSeverity حرفيًا (rin_editor_engine.h) بنفس ترتيب القيم. */
    object DiagnosticSeverity {
        const val ERROR = 0
        const val WARNING = 1
        const val NOTE = 2
        const val HELP = 3
    }

    private fun requireHandle(): Long {
        check(!destroyed) { "RinNativeEditor already destroyed" }
        return handle
    }

    /**
     * نص [line] كما يراه المحرك حاليًا، مع تثبيت رقم السطر ضمن الحدود الفعلية أولاً (نفس منطق
     * clampLine في C++). يُستخدم فقط كأساس لتحويل بايت↔حرف أدناه؛ سطر خارج الحدود (مثال: -1 من
     * سهم لأعلى عند أول سطر) يُطابَق مع نفس السطر الذي سيُثبَّت إليه المحرك أصلاً، بدل إرجاع نص
     * فارغ يُفقِد عمود المؤشر المطلوب أثناء التحويل.
     */
    private fun clampedLineText(line: Int): String {
        val count = nativeGetLineCount(requireHandle())
        val clampedLine = line.coerceIn(0, (count - 1).coerceAtLeast(0))
        return nativeGetLine(requireHandle(), clampedLine)
    }

    /** فهرس-حرف UTF-16 → إزاحة-بايت UTF-8، بالنسبة لنص [line] الحالي. */
    private fun charToByte(line: Int, charCol: Int): Int =
        charIndexToByteOffset(clampedLineText(line), charCol)

    /** إزاحة-بايت UTF-8 (قادمة من C++) → فهرس-حرف UTF-16، بالنسبة لنص [line] الحالي. */
    private fun byteToChar(line: Int, byteCol: Int): Int =
        byteOffsetToCharIndex(clampedLineText(line), byteCol)

    /** يُستدعى عند التخلّص النهائي من المحرر (مثلاً View.onDetachedFromWindow) لتحرير الذاكرة الأصلية. */
    fun destroy() {
        if (!destroyed) {
            nativeDestroy(handle)
            handle = 0L
            destroyed = true
        }
    }

    fun setText(text: String) = nativeSetText(requireHandle(), text)
    fun getText(): String = nativeGetText(requireHandle())
    fun lineCount(): Int = nativeGetLineCount(requireHandle())
    fun getLine(line: Int): String = nativeGetLine(requireHandle(), line)

    fun getCursor(): Pos {
        val a = nativeGetCursor(requireHandle())
        return Pos(a[0], byteToChar(a[0], a[1]))
    }

    fun getSelection(): Selection {
        val a = nativeGetSelection(requireHandle())
        val startLine = a[1]; val endLine = a[3]
        return Selection(
            a[0] == 1,
            Pos(startLine, byteToChar(startLine, a[2])),
            Pos(endLine, byteToChar(endLine, a[4]))
        )
    }

    fun setCursor(line: Int, col: Int, extend: Boolean = false) =
        nativeSetCursor(requireHandle(), line, charToByte(line, col), extend)

    fun setSelection(aLine: Int, aCol: Int, bLine: Int, bCol: Int) =
        nativeSetSelection(requireHandle(), aLine, charToByte(aLine, aCol), bLine, charToByte(bLine, bCol))

    fun collapseSelection() = nativeCollapseSelection(requireHandle())

    /** يُدرج [text] عند المؤشر. [smart] يفعّل الإزاحة التلقائية/إغلاق الأقواس (اتركها true للكتابة العادية). */
    fun insertText(text: String, smart: Boolean = true): Pos {
        val a = nativeInsertText(requireHandle(), text, smart)
        return Pos(a[0], byteToChar(a[0], a[1]))
    }

    fun deleteBackward(): Pos { val a = nativeDeleteBackward(requireHandle()); return Pos(a[0], byteToChar(a[0], a[1])) }
    fun deleteForward(): Pos { val a = nativeDeleteForward(requireHandle()); return Pos(a[0], byteToChar(a[0], a[1])) }

    fun replaceRange(sl: Int, sc: Int, el: Int, ec: Int, text: String): Pos {
        // التحويل يجب أن يحدث *قبل* الاستبدال: بعد الاستبدال قد لا يكون السطر el موجودًا أصلاً.
        val scByte = charToByte(sl, sc)
        val ecByte = charToByte(el, ec)
        val a = nativeReplaceRange(requireHandle(), sl, scByte, el, ecByte, text)
        return Pos(a[0], byteToChar(a[0], a[1]))
    }

    /** يُرجع null إن لم يبقَ شيء للتراجع عنه. */
    fun undo(): Pos? = nativeUndo(requireHandle())?.let { Pos(it[0], byteToChar(it[0], it[1])) }
    fun redo(): Pos? = nativeRedo(requireHandle())?.let { Pos(it[0], byteToChar(it[0], it[1])) }

    fun duplicateCurrentLine(): Pos { val a = nativeDuplicateLine(requireHandle()); return Pos(a[0], byteToChar(a[0], a[1])) }
    fun deleteCurrentLine(): Pos { val a = nativeDeleteLine(requireHandle()); return Pos(a[0], byteToChar(a[0], a[1])) }
    fun moveLineUp(): Pos { val a = nativeMoveLineUp(requireHandle()); return Pos(a[0], byteToChar(a[0], a[1])) }
    fun moveLineDown(): Pos { val a = nativeMoveLineDown(requireHandle()); return Pos(a[0], byteToChar(a[0], a[1])) }
    fun toggleLineComment(): Pos { val a = nativeToggleComment(requireHandle()); return Pos(a[0], byteToChar(a[0], a[1])) }
    fun indentSelection(): Pos { val a = nativeIndentSelection(requireHandle()); return Pos(a[0], byteToChar(a[0], a[1])) }
    fun unindentSelection(): Pos { val a = nativeUnindentSelection(requireHandle()); return Pos(a[0], byteToChar(a[0], a[1])) }

    /** -1 = متوازن، وإلا رقم أول سطر فيه خلل (1-based). لا عمود هنا فلا حاجة لتحويل. */
    fun checkBracketBalance(): Int = nativeCheckBracketBalance(requireHandle())

    fun getHighlights(): List<Highlight> {
        val flat = nativeGetHighlightSpansFlat(requireHandle())
        val out = ArrayList<Highlight>(flat.size / 4)
        var i = 0
        // ذاكرة مؤقتة لسطر واحد: امتدادات نفس السطر متتالية غالبًا في الناتج المسطّح، فتفادي
        // إعادة قراءة نص السطر عبر JNI لكل امتداد يقلّل التكلفة بشكل كبير على ملف كبير.
        var cachedLine = -1
        var cachedText = ""
        while (i + 3 < flat.size) {
            val line = flat[i]
            if (line != cachedLine) { cachedText = clampedLineText(line); cachedLine = line }
            val startCol = byteOffsetToCharIndex(cachedText, flat[i + 1])
            val endCol = byteOffsetToCharIndex(cachedText, flat[i + 2])
            out.add(Highlight(line, startCol, endCol, flat[i + 3]))
            i += 4
        }
        return out
    }

    fun findAll(query: String, caseSensitive: Boolean): List<FindMatch> {
        if (query.isEmpty()) return emptyList()
        val flat = nativeFindAllFlat(requireHandle(), query, caseSensitive)
        val out = ArrayList<FindMatch>(flat.size / 3)
        var i = 0
        var cachedLine = -1
        var cachedText = ""
        while (i + 2 < flat.size) {
            val line = flat[i]
            if (line != cachedLine) { cachedText = clampedLineText(line); cachedLine = line }
            val startCol = byteOffsetToCharIndex(cachedText, flat[i + 1])
            val endCol = byteOffsetToCharIndex(cachedText, flat[i + 2])
            out.add(FindMatch(line, startCol, endCol))
            i += 3
        }
        return out
    }

    /** اقتراحات إكمال تلقائي (كلمات محجوزة للغة Rin + معرِّفات المستند) تبدأ بـ [prefix]. */
    /** توافق واجهة RinCodeEditorView القديمة. التلوين هنا Rin أصلاً من rin::Lexer في C++. */
    /**
     * يضبط لغة التلوين/التشخيص حسب امتداد الملف. فقط "passkit" يغيّر السلوك؛ كل امتداد آخر يبقى
     * Rin كما كان (المحرك الأصلي يلوّن بـ rin::Lexer الحقيقي). فشل الاستدعاء الأصلي لا يُسقط التطبيق.
     */
    fun setLanguage(extension: String) {
        try {
            nativeSetLanguage(requireHandle(), extension)
        } catch (t: Throwable) {
            android.util.Log.w("RinNativeEditor", "setLanguage($extension) failed", t)
        }
    }

    fun collectSuggestions(prefix: String, maxResults: Int = 20): List<String> =
        getSuggestions(prefix, maxResults)

    fun getSuggestions(prefix: String, maxResults: Int = 20): List<String> =
        nativeGetSuggestions(requireHandle(), prefix, maxResults).toList()

    fun lineStartPosition(oneBasedLine: Int): Pos {
        val a = nativeLineStartPosition(requireHandle(), oneBasedLine)
        return Pos(a[0], byteToChar(a[0], a[1]))
    }

    /**
     * تشخيص أخطاء الصياغة الحي (rin::Lexer + rin::Parser، بدون تشغيل المفسِّر) — انظر
     * rinedit::EditorEngine::computeDiagnostics(). يُفكّك الحزم النصية القادمة من
     * nativeGetDiagnosticsPacked (line:startCol:endCol:severity:code:message) ويحوّل الأعمدة
     * من إزاحة-بايت UTF-8 إلى فهرس-حرف UTF-16، بنفس نمط [getHighlights]/[findAll].
     */
    fun getDiagnostics(): List<Diagnostic> {
        val packed = nativeGetDiagnosticsPacked(requireHandle())
        val out = ArrayList<Diagnostic>(packed.size)
        var cachedLine = -1
        var cachedText = ""
        for (entry in packed) {
            // حدّ 6 أجزاء: الرسالة نفسها (الجزء السادس/الأخير) قد تحوي ':' فلا تُقطَع.
            val parts = entry.split(":", limit = 6)
            if (parts.size < 6) continue
            val line = parts[0].toIntOrNull() ?: continue
            val startColByte = parts[1].toIntOrNull() ?: continue
            val endColByte = parts[2].toIntOrNull() ?: continue
            val severity = parts[3].toIntOrNull() ?: continue
            val code = parts[4]
            val message = parts[5]
            if (line != cachedLine) { cachedText = clampedLineText(line); cachedLine = line }
            val startCol = byteOffsetToCharIndex(cachedText, startColByte)
            val endCol = byteOffsetToCharIndex(cachedText, endColByte)
            out.add(Diagnostic(line, startCol, endCol, severity, code, message))
        }
        return out
    }

    // --- أدوات احترافية: طيّ، هيكل، إعادة محاذاة، قوالب ---------------------------------

    /** نطاق طيّ: سطور صفرية، [endLine] شاملة ولا تشمل سطر القوس الختامي. */
    data class FoldRange(val startLine: Int, val endLine: Int)

    /** عنصر في هيكل الملف. [kind]: [OutlineKind]. العمود فهرس-حرف UTF-16. */
    data class OutlineItem(val line: Int, val col: Int, val kind: Int, val name: String)

    object OutlineKind { const val FUNCTION = 0; const val CLASS = 1; const val VARIABLE = 2; const val LABEL = 3 }

    fun getFoldRanges(): List<FoldRange> {
        val flat = nativeGetFoldRangesFlat(requireHandle())
        return (0 until flat.size / 2).map { FoldRange(flat[it * 2], flat[it * 2 + 1]) }
    }

    fun getOutline(): List<OutlineItem> {
        val out = ArrayList<OutlineItem>()
        for (p in nativeGetOutlinePacked(requireHandle())) {
            val parts = p.split(":", limit = 4)
            if (parts.size < 4) continue
            val line = parts[0].toIntOrNull() ?: continue
            val byteCol = parts[1].toIntOrNull() ?: continue
            val kind = parts[2].toIntOrNull() ?: continue
            out.add(OutlineItem(line, byteToChar(line, byteCol), kind, parts[3]))
        }
        return out
    }

    /** يعيد محاذاة المستند كاملاً حسب عمق الأقواس (أمر تراجع واحد). false إن لم يتغيّر شيء أو كان النص غير صالح نحوياً. */
    fun reindentDocument(indentWidth: Int = 4): Boolean = nativeReindentDocument(requireHandle(), indentWidth)

    /** يوسّع القالب المنتهي عند المؤشر (each/repeat/...)؛ يُرجع موضع المؤشر الجديد أو null. */
    fun expandSnippetAtCursor(): Pos? =
        nativeExpandSnippet(requireHandle())?.let { Pos(it[0], byteToChar(it[0], it[1])) }

    // --- IntelliSense (rin_intellisense.h) -----------------------------------------------

    /** نوع الاقتراح — يطابق intel::Kind في C++ بالترتيب. */
    object CompletionKind {
        const val KEYWORD = 0; const val FUNCTION = 1; const val METHOD = 2; const val VARIABLE = 3; const val CONSTANT = 4
        const val CLASS = 5; const val SNIPPET = 6; const val NAMESPACE = 7; const val PROPERTY = 8; const val TEMPLATE = 9
        const val LABEL = 10; const val PARAMETER = 11; const val TAG = 12; const val MODULE = 13; const val WORD = 14
        const val ENUM = 15; const val ENUM_MEMBER = 16; const val INTERFACE = 17
    }

    /** مصدر الإكمال: يطابق intel::Trigger. */
    object CompletionTrigger { const val NONE = 0; const val MEMBER = 1; const val AT = 2; const val HASH = 3; const val IMPORT_PATH = 4 }

    /** اقتراح واحد. [matchPos] مواضع الحروف المطابقة داخل [label] (فهارس UTF-16). [cursorBack] بالحروف. */
    class CompletionItem(
        val kind: Int, val label: String, val insertText: String, val detail: String, val doc: String,
        val cursorBack: Int, val snippet: Boolean, val matchPos: IntArray
    )

    /** نتيجة الإكمال: [startCol]..[endCol] (فهارس حروف على سطر المؤشر) هو النص الذي يُستبدَل. */
    class CompletionResult(val startCol: Int, val endCol: Int, val trigger: Int, val items: List<CompletionItem>)

    class SignatureInfo(val kind: Int, val name: String, val ret: String, val doc: String, val active: Int, val params: List<String>)

    class SymbolDoc(val name: String, val kindName: String, val signature: String, val doc: String, val defLine: Int, val defCol: Int)

    /**
     * إكمال سياقي عند المؤشر. null إن لم يتوفر شيء (تحديد/تعليق/نص/موضع تسمية...).
     * [explicitInvoke] = Ctrl+Space (يسمح ببادئة فارغة).
     */
    fun complete(explicitInvoke: Boolean = false, maxResults: Int = 60): CompletionResult? {
        val packed = try { nativeComplete(requireHandle(), explicitInvoke, maxResults) } catch (t: Throwable) {
            android.util.Log.w("RinNativeEditor", "complete failed", t); null
        } ?: return null
        if (packed.size < 2) return null
        val header = packed[0].split('\u001F')
        if (header.size < 3) return null
        val cursorLine = getCursor().line
        val lineText = clampedLineText(cursorLine)
        val startCol = byteOffsetToCharIndex(lineText, header[0].toIntOrNull() ?: return null)
        val endCol = byteOffsetToCharIndex(lineText, header[1].toIntOrNull() ?: return null)
        val trigger = header[2].toIntOrNull() ?: 0
        val items = ArrayList<CompletionItem>(packed.size - 1)
        for (i in 1 until packed.size) {
            val f = packed[i].split('\u001F', limit = 8)
            if (f.size < 8) continue
            val label = f[1]
            val insert = f[2]
            val backBytes = f[5].toIntOrNull() ?: 0
            // cursorBack بالبايت في C++؛ الحروف المرجوعة دائماً ASCII (')' أو '"') فالقيمتان متطابقتان.
            val pos = if (f[7].isEmpty()) IntArray(0)
                      else f[7].split(',').mapNotNull { it.toIntOrNull() }.map { byteOffsetToCharIndex(label, it) }.toIntArray()
            items.add(CompletionItem(f[0].toIntOrNull() ?: 0, label, insert, f[3], f[4], backBytes, f[6] == "1", pos))
        }
        return if (items.isEmpty()) null else CompletionResult(startCol, endCol, trigger, items)
    }

    /** توقيع الدالة التي يقف المؤشر داخل استدعائها (أو null). */
    fun signatureHelp(): SignatureInfo? {
        val raw = try { nativeSignatureHelp(requireHandle()) } catch (t: Throwable) { null } ?: return null
        val f = raw.split('\u001F', limit = 6)
        if (f.size < 6) return null
        val params = if (f[5].isEmpty()) emptyList() else f[5].split('\u001E')
        return SignatureInfo(f[0].toIntOrNull() ?: 0, f[1], f[2], f[3], f[4].toIntOrNull() ?: 0, params)
    }

    /** توثيق الرمز عند ([line], [col]) — للضغط الطويل/hover. */
    fun describeAt(line: Int, col: Int): SymbolDoc? {
        val raw = try { nativeDescribe(requireHandle(), line, charToByte(line, col)) } catch (t: Throwable) { null } ?: return null
        val f = raw.split('\u001F', limit = 6)
        if (f.size < 6) return null
        return SymbolDoc(f[0], f[1], f[2], f[3], f[4].toIntOrNull() ?: -1, f[5].toIntOrNull() ?: -1)
    }

    /** يسجّل قبول اقتراح ليصعد في الترتيب لاحقاً. */
    fun recordCompletionUse(label: String) {
        try { nativeRecordCompletionUse(label) } catch (_: Throwable) {}
    }
}
