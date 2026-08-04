package com.dlof.rinlang.store.projects

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Typeface
import android.util.Base64
import com.dlof.rinlang.Project
import com.dlof.rinlang.ProjectManager
import java.io.ByteArrayOutputStream

/**
 * يولّد صورة مصغّرة (thumbnail) لمشروع Rin **من كوده الحقيقي مباشرة** — لا صورة تسويقية ولا
 * أيقونة عامة، بل رسم فعلي لسطور أحد ملفات .rin الحقيقية داخل المشروع، بشكل يُشبه لقطة شاشة
 * لمحرّر Rin (شريط عنوان مزيّف بثلاث نقاط + أرقام أسطر + تلوين مبسَّط للصيغة النحوية)، ثم
 * تُرمَّز النتيجة base64 لتُخزَّن في [RinProject.thumbnailBase64] بنفس أسلوب بقية صور المتجر
 * (بلا Firebase Storage مدفوع).
 *
 * التلوين هنا مبسَّط عمداً (كلمات مفتاحية / نصوص / أرقام / تعليقات فقط) مقارنة بـ
 * [com.dlof.rinlang.RinSyntaxHighlighter] الكامل المستخدَم في المحرّر نفسه، لأن الهدف صورة
 * صغيرة سريعة التوليد لا محرّراً تفاعلياً.
 */
object CodeThumbnailGenerator {

    private const val WIDTH_PX = 720
    private const val HEIGHT_PX = 405
    private const val MAX_LINES = 13
    private const val MAX_CHARS_PER_LINE = 58

    private const val COLOR_BACKGROUND = "#1B1D22"
    private const val COLOR_TITLEBAR = "#22252B"
    private const val COLOR_LINE_NUMBER = "#4A4E58"
    private const val COLOR_TEXT = "#E3E5E8"
    private const val COLOR_KEYWORD = "#569CD6"
    private const val COLOR_CONTAINER_KEYWORD = "#4EC9B0"
    private const val COLOR_STRING = "#CE9178"
    private const val COLOR_NUMBER = "#B5CEA8"
    private const val COLOR_COMMENT = "#6A9955"

    private val coreKeywords = setOf(
        "let", "print", "if", "else", "while", "fun", "return", "break", "continue",
        "true", "false", "nil", "and", "or"
    )
    private val containerKeywords = setOf(
        "text", "container", "Containers", "Group", "Volume", "Section",
        "Translations", "translation", "link", "tying", "merge",
        "installation", "simplified", "save", "file", "end",
        "row", "style", "document", "route", "data", "api", "import", "table", "doc", "portal", "block", "pipe"
    )

    /**
     * يختار "الملف الرئيسي" الأنسب لتوليد الصورة المصغّرة منه: يفضّل main.rin إن وُجد، وإلا
     * أكبر ملف .rin (بعدد الأسطر غير الفارغة) — الأرجح أن يكون الملف الأكثر تمثيلاً للمشروع.
     * يرجع null إن لم يحتوِ المشروع أي ملف .rin على الإطلاق.
     */
    private fun pickMainFile(project: Project): com.dlof.rinlang.RinFile? {
        val rinFiles = ProjectManager.listFiles(project).filter { it.name.endsWith(".rin", ignoreCase = true) }
        if (rinFiles.isEmpty()) return null
        rinFiles.find { it.name.equals("main.rin", ignoreCase = true) }?.let { return it }
        return rinFiles.maxByOrNull { file ->
            try {
                ProjectManager.readFile(file).lines().count { it.isNotBlank() }
            } catch (t: Throwable) {
                0
            }
        }
    }

    /**
     * يولّد الصورة المصغّرة النهائية base64 لمشروع [project]، أو null إن لم يوجد أي ملف .rin
     * صالح فيه لعرضه (لا يُفشِل عملية النشر — المستدعي يستخدم بديلاً افتراضياً في هذه الحالة).
     * يرجع أيضاً اسم الملف المصدر لعرضه كتلميح في بطاقة المشروع.
     */
    fun generateFromProject(project: Project): Pair<String, String>? {
        val mainFile = pickMainFile(project) ?: return null
        val content = try {
            ProjectManager.readFile(mainFile)
        } catch (t: Throwable) {
            return null
        }
        val base64 = generateBase64(content, mainFile.name) ?: return null
        return base64 to mainFile.name
    }

    /** يرسم [sourceCode] كصورة Bitmap ويرمّزها base64 (JPEG)، أو null إن فشل الرسم. */
    fun generateBase64(sourceCode: String, fileName: String): String? {
        return try {
            val bitmap = render(sourceCode, fileName)
            val out = ByteArrayOutputStream()
            bitmap.compress(Bitmap.CompressFormat.JPEG, 88, out)
            Base64.encodeToString(out.toByteArray(), Base64.NO_WRAP)
        } catch (t: Throwable) {
            null
        }
    }

    private fun render(sourceCode: String, fileName: String): Bitmap {
        val bitmap = Bitmap.createBitmap(WIDTH_PX, HEIGHT_PX, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bitmap)
        canvas.drawColor(Color.parseColor(COLOR_BACKGROUND))

        // شريط عنوان مزيَّف بثلاث نقاط (أحمر/أصفر/أخضر) بنفس هوية نوافذ المحررات، واسم الملف
        // الحقيقي — يوضّح فوراً أن هذه لقطة "كود حقيقي" لا صورة عامة.
        val titleBarHeight = 34f
        val titlePaint = Paint().apply { color = Color.parseColor(COLOR_TITLEBAR); isAntiAlias = true }
        canvas.drawRect(0f, 0f, WIDTH_PX.toFloat(), titleBarHeight, titlePaint)

        val dotRadius = 5f
        val dotColors = listOf("#FF5F57", "#FEBC2E", "#28C840")
        dotColors.forEachIndexed { index, hex ->
            val dotPaint = Paint().apply { color = Color.parseColor(hex); isAntiAlias = true }
            canvas.drawCircle(20f + index * 16f, titleBarHeight / 2f, dotRadius, dotPaint)
        }

        val fileNamePaint = Paint().apply {
            color = Color.parseColor(COLOR_LINE_NUMBER)
            isAntiAlias = true
            textSize = 15f
            typeface = Typeface.MONOSPACE
            textAlign = Paint.Align.CENTER
        }
        canvas.drawText(fileName.take(40), WIDTH_PX / 2f, titleBarHeight / 2f + 5f, fileNamePaint)

        // أسطر الكود الحقيقية
        val codeTextSize = 15.5f
        val lineHeight = 27f
        val gutterWidth = 34f
        val leftPadding = gutterWidth + 12f
        var y = titleBarHeight + 26f

        val lineNumberPaint = Paint().apply {
            color = Color.parseColor(COLOR_LINE_NUMBER)
            isAntiAlias = true
            textSize = codeTextSize
            typeface = Typeface.MONOSPACE
            textAlign = Paint.Align.RIGHT
        }

        val lines = sourceCode.lines()
        val visibleLines = lines.take(MAX_LINES)
        visibleLines.forEachIndexed { index, rawLine ->
            canvas.drawText((index + 1).toString(), gutterWidth, y, lineNumberPaint)
            val truncated = if (rawLine.length > MAX_CHARS_PER_LINE) rawLine.take(MAX_CHARS_PER_LINE) + "…" else rawLine
            drawHighlightedLine(canvas, truncated, leftPadding, y, codeTextSize)
            y += lineHeight
        }

        if (lines.size > MAX_LINES) {
            val morePaint = Paint().apply {
                color = Color.parseColor(COLOR_LINE_NUMBER)
                isAntiAlias = true
                textSize = codeTextSize
                typeface = Typeface.MONOSPACE
            }
            canvas.drawText("⋯ +${lines.size - MAX_LINES} أسطر أخرى", leftPadding, y, morePaint)
        }

        return bitmap
    }

    /** يرسم سطراً واحداً بتلوين مبسَّط: تعليق كامل، أو تناوب نصوص/أرقام/كلمات مفتاحية داخل باقي السطر. */
    private fun drawHighlightedLine(canvas: Canvas, line: String, startX: Float, y: Float, textSize: Float) {
        val basePaint = Paint().apply {
            isAntiAlias = true
            this.textSize = textSize
            typeface = Typeface.MONOSPACE
        }

        val trimmedStart = line.trimStart()
        if (trimmedStart.startsWith("//")) {
            basePaint.color = Color.parseColor(COLOR_COMMENT)
            canvas.drawText(line, startX, y, basePaint)
            return
        }

        var x = startX
        var i = 0
        val tokenRegex = Regex("\"[^\"]*\"|'[^']*'|\\b\\d+\\.?\\d*\\b|[A-Za-z_][A-Za-z0-9_]*|.")
        for (match in tokenRegex.findAll(line)) {
            val token = match.value
            basePaint.color = when {
                token.startsWith("\"") || token.startsWith("'") -> Color.parseColor(COLOR_STRING)
                token.firstOrNull()?.isDigit() == true -> Color.parseColor(COLOR_NUMBER)
                token in coreKeywords -> Color.parseColor(COLOR_KEYWORD)
                token in containerKeywords -> Color.parseColor(COLOR_CONTAINER_KEYWORD)
                else -> Color.parseColor(COLOR_TEXT)
            }
            canvas.drawText(token, x, y, basePaint)
            x += basePaint.measureText(token)
            i++
        }
    }
}
