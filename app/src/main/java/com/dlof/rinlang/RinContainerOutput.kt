package com.dlof.rinlang

import java.util.Locale

/**
 * عائلة الحاوية كما تظهر في مخرجات المحرّك (`containerTagName` / `containerIcon` في rin_interpreter.cpp)،
 * مع هوية بصرية خاصة بكل عائلة: أيقونة نصية، تسمية عربية، ولون مميّز [accent] بصيغة ARGB.
 * الألوان هنا ثابتة في الكود عمداً (لا موارد جديدة تُضاف إلى colors.xml).
 */
enum class ContainerFamily(val glyph: String, val label: String, val accent: Int) {
    PIPE("🧵", "أنبوب", 0xFF26A69A.toInt()),
    DATA("🗂️", "بيانات", 0xFF5C6BC0.toInt()),
    API("🌐", "واجهة API", 0xFF29B6F6.toInt()),
    IMPORT("📦", "استيراد", 0xFF8D6E63.toInt()),
    TABLE("📊", "جدول", 0xFF66BB6A.toInt()),
    DOC("🧾", "مستند", 0xFFFFA726.toInt()),
    OBJECT("🧩", "كائن", 0xFFAB47BC.toInt()),
    PORTAL("🎨", "بوابة", 0xFFEC407A.toInt()),
    BLOCK("🧱", "كتلة", 0xFF78909C.toInt()),
    STICKER("🏷️", "ملصق", 0xFFFFCA28.toInt()),
    CHATBOT("💬", "روبوت محادثة", 0xFF42A5F5.toInt()),
    MAKE("🛠️", "وحدة Make", 0xFFFF7043.toInt()),
    SQL("🔎", "SQL", 0xFF26C6DA.toInt()),
    GROUP("🗂️", "مجموعة", 0xFF7E57C2.toInt()),
    VOLUME("📚", "مجلّد", 0xFF3949AB.toInt()),
    SECTION("🔹", "قسم", 0xFF9E9E9E.toInt()),
    GENERIC("📦", "حاوية", 0xFF90A4AE.toInt());

    companion object {
        /** يستنتج العائلة من الوسم المطبوع (`container.pipe` / `make.xxx` / `Volume` ...). */
        fun fromTag(tag: String): ContainerFamily {
            val t = tag.lowercase(Locale.ROOT)
            return when {
                t.startsWith("make.") || t == "container.make" -> MAKE
                t == "containers.group" -> GROUP
                t == "volume" -> VOLUME
                t == "section" -> SECTION
                t == "container.pipe" -> PIPE
                t == "container.data" -> DATA
                t == "container.api" -> API
                t == "container.import" -> IMPORT
                t == "container.table" -> TABLE
                t == "container.doc" -> DOC
                t == "container.object" -> OBJECT
                t == "container.portal" -> PORTAL
                t == "container.block" -> BLOCK
                t == "container.sticker" -> STICKER
                t == "container.chatbot" -> CHATBOT
                t == "container.sql" -> SQL
                else -> GENERIC
            }
        }
    }
}

/** سطر فتح أم إغلاق. */
enum class ContainerRole { OPEN, CLOSE }

/**
 * علامة على سطر مخرجات يمثّل بداية أو نهاية حاوية. [name] null للحاويات المجهولة؛ [members] لا يُملأ
 * إلا في سطر إغلاق Group/Volume الذي يطبع `[تحتوي: a, b]`.
 */
data class RinContainerMark(
    val role: ContainerRole,
    val tag: String,
    val name: String?,
    val members: List<String> = emptyList()
) {
    val family: ContainerFamily get() = ContainerFamily.fromTag(tag)
}

/** يتعرّف على أسطر فتح/إغلاق الحاويات بنفس الصيغ الحرفية التي يكتبها المفسِّر. لا يرمي أبداً. */
object RinContainerParser {

    private const val TAG =
        "(container(?:\\.[A-Za-z]+)?|make\\.[\\w.\\-]+|Containers\\.Group|Volume|Section)"

    // "<glyph> <tag>[ = name]" بعد إزالة الرمز الأول
    private val RE_OPEN = Regex("^" + TAG + "(?:\\s*=\\s*(.+))?$")

    // "✅|◽ .end/<tag>[ (name)][ [تحتوي: a, b]]"
    private val RE_CLOSE = Regex(
        "^[\\u2705\\u25FD]\\s*\\.end/" + TAG +
            "(?:\\s+\\((.+?)\\))?(?:\\s+\\[تحتوي:\\s*(.+?)\\])?\\s*$"
    )

    fun parse(trimmedLine: String): RinContainerMark? {
        if (trimmedLine.isEmpty()) return null
        return parseClose(trimmedLine) ?: parseOpen(trimmedLine)
    }

    private fun parseClose(line: String): RinContainerMark? {
        val m = RE_CLOSE.find(line) ?: return null
        val members = m.groupValues[3]
            .split(",")
            .map { it.trim() }
            .filter { it.isNotEmpty() }
        return RinContainerMark(
            role = ContainerRole.CLOSE,
            tag = m.groupValues[1],
            name = m.groupValues[2].ifEmpty { null },
            members = members
        )
    }

    private fun parseOpen(line: String): RinContainerMark? {
        val sp = line.indexOf(' ')
        if (sp <= 0) return null
        // الرمز الأول أيقونة فقط (إيموجي): لا حروف ولا أرقام ولا علامات بداية نصية عادية.
        val glyph = line.substring(0, sp)
        if (glyph.any { it.isLetterOrDigit() || it == '[' || it == '(' || it == '"' || it == '<' }) return null
        val m = RE_OPEN.find(line.substring(sp + 1).trim()) ?: return null
        return RinContainerMark(
            role = ContainerRole.OPEN,
            tag = m.groupValues[1],
            name = m.groupValues[2].ifEmpty { null }
        )
    }
}

/** عقدة في شجرة المخرجات: سطر عادي أو حاوية مفتوحة بما فيها. */
sealed class RinOutputNode {

    class Leaf(val line: RinLogLine) : RinOutputNode()

    class Block(val open: RinLogLine, val mark: RinContainerMark) : RinOutputNode() {
        val children: MutableList<RinOutputNode> = ArrayList()
        var close: RinLogLine? = null
        var closeMark: RinContainerMark? = null

        /** false إذا توقّف التنفيذ (مثلاً بخطأ) قبل سطر الإغلاق. */
        val isClosed: Boolean get() = closeMark != null

        /** عدد الأسطر العادية داخل هذه الحاوية بما فيها المتداخلة (السطر المكرَّر ×N يُحتسب N). */
        fun contentLineCount(): Int {
            var total = 0
            for (child in children) {
                total += when (child) {
                    is Leaf -> maxOf(child.line.repeat, 1)
                    is Block -> child.contentLineCount()
                }
            }
            return total
        }
    }
}

object RinContainerTree {

    private fun matches(open: RinContainerMark, close: RinContainerMark): Boolean =
        open.tag.equals(close.tag, ignoreCase = true) && open.name == close.name

    /**
     * يبني شجرة من أسطر مسطّحة بمطابقة فتح/إغلاق. إغلاق بلا فتح يبقى سطراً عادياً؛ وحاوية لم تُغلق
     * (بسبب خطأ أثناء التنفيذ) تبقى كتلة بـ [RinOutputNode.Block.isClosed] = false بدل ضياع ما بداخلها.
     * إن جاء إغلاق يطابق كتلة أبعد في المكدّس، فالكتل الأحدث منها تُعدّ غير مغلقة.
     */
    fun build(lines: List<RinLogLine>): List<RinOutputNode> {
        val root = ArrayList<RinOutputNode>()
        val stack = ArrayList<RinOutputNode.Block>()

        fun sink(): MutableList<RinOutputNode> =
            if (stack.isEmpty()) root else stack[stack.size - 1].children

        for (line in lines) {
            val mark = line.container
            if (mark == null) {
                sink().add(RinOutputNode.Leaf(line))
            } else if (mark.role == ContainerRole.OPEN) {
                val block = RinOutputNode.Block(line, mark)
                sink().add(block)
                stack.add(block)
            } else {
                var idx = stack.size - 1
                while (idx >= 0 && !matches(stack[idx].mark, mark)) idx--
                if (idx < 0) {
                    sink().add(RinOutputNode.Leaf(line))
                } else {
                    while (stack.size - 1 > idx) stack.removeAt(stack.size - 1)
                    val block = stack.removeAt(stack.size - 1)
                    block.close = line
                    block.closeMark = mark
                }
            }
        }
        return root
    }

    /** نص شجري قابل للنسخ والمشاركة: كل حاوية بعنوانها وخط عمودي لمحتواها وسطر ختام. */
    fun toTreeText(lines: List<RinLogLine>): String {
        val sb = StringBuilder()
        fun walk(nodes: List<RinOutputNode>, prefix: String) {
            for (node in nodes) {
                when (node) {
                    is RinOutputNode.Leaf -> sb.append(prefix).append(node.line.text).append('\n')
                    is RinOutputNode.Block -> {
                        val m = node.mark
                        sb.append(prefix).append(m.family.glyph).append(' ').append(m.family.label)
                        if (m.name != null) sb.append(" = ").append(m.name)
                        sb.append('\n')
                        walk(node.children, "$prefix│ ")
                        sb.append(prefix).append(if (node.isClosed) "└ انتهت" else "└ ⚠ لم تُغلق")
                        val members = node.closeMark?.members.orEmpty()
                        if (members.isNotEmpty()) {
                            sb.append(" [تحتوي: ").append(members.joinToString(", ")).append(']')
                        }
                        sb.append('\n')
                    }
                }
            }
        }
        walk(build(lines), "")
        return sb.toString().trimEnd()
    }
}
