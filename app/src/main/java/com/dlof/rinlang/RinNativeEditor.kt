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
    fun setLanguage(extension: String) { /* Native Rin editor is intentionally language-source driven. */ }

    fun collectSuggestions(prefix: String, maxResults: Int = 20): List<String> =
        getSuggestions(prefix, maxResults)

    fun getSuggestions(prefix: String, maxResults: Int = 20): List<String> =
        nativeGetSuggestions(requireHandle(), prefix, maxResults).toList()

    fun lineStartPosition(oneBasedLine: Int): Pos {
        val a = nativeLineStartPosition(requireHandle(), oneBasedLine)
        return Pos(a[0], byteToChar(a[0], a[1]))
    }
}
