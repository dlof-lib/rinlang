package com.dlof.rinlang

/**
 * مخطّط مشروع احترافي: بنية مجلدات مرتّبة + ملفات توثيق + اختبارات + استيراد المكتبات المختارة.
 * منطق خالص (بلا أندرويد) — يُنتج قائمة ملفات ونصاً جديداً لـ main.rin، ويطبّقها [ProjectManager.applyBlueprint].
 *
 * البنية الناتجة فوق ما يولّده ProjectManager.createProject أصلاً (main.rin وindsin/):
 * ```
 * <المشروع>/
 * ├── main.rin              ← نقطة الدخول، تبدأ بكتلة استيراد المكتبات
 * ├── rin.project.json      ← بيانات المشروع والمكتبات المستخدمة
 * ├── README.md
 * ├── .gitignore
 * ├── src/utils.rin         ← وحدة مشتركة (export fun …) يستوردها main.rin
 * ├── tests/test_main.rin   ← اختبارات assert تعمل بـ `rin test tests`
 * ├── docs/overview.md
 * └── assets/
 * ```
 * كل مسارات الاستيراد نسبية للملف المستورِد (تعمل على المفسّر الأصلي، تحقّقنا بـ `rin` CLI).
 */
data class ProjectBlueprint(
    val name: String,
    /** معرّف النوع كما في [ProjectType.id]: container/table/ui/free/illust/html/html_js. */
    val typeId: String,
    val description: String = "",
    val author: String = "",
    val version: String = "0.1.0",
    /** مسارات الاستيراد الكاملة للمكتبات المختارة، مثل "lib/math.og.rin". */
    val libraries: List<String> = emptyList(),
    val layout: Layout = Layout(),
    /** لغة النصوص المولّدة (README/تعليقات): "ar" أو "en". */
    val lang: String = "ar"
) {
    data class Layout(
        val readme: Boolean = true,
        val gitignore: Boolean = true,
        val srcModule: Boolean = true,
        val tests: Boolean = true,
        val docs: Boolean = true,
        val assets: Boolean = true,
        val manifest: Boolean = true
    )

    val isWeb: Boolean get() = typeId == "html" || typeId == "html_js"
    private val en: Boolean get() = lang == "en"

    /** هل يتضمن المشروع وحدة src/utils.rin؟ (غير مجدية لمشاريع الويب). */
    val hasSrc: Boolean get() = layout.srcModule && !isWeb
    val hasTests: Boolean get() = layout.tests && hasSrc

    /** ملف/مجلد مخطَّط إنشاؤه. المجلد يُعلَّم بـ [dir]. */
    data class Entry(val path: String, val content: String = "", val dir: Boolean = false)

    // ------------------------------------------------------------------ الخطة

    fun plan(createdAt: String = ""): List<Entry> {
        val out = ArrayList<Entry>()
        if (layout.manifest) out.add(Entry("rin.project.json", manifestJson(createdAt)))
        if (layout.readme) out.add(Entry("README.md", readme()))
        if (layout.gitignore) out.add(Entry(".gitignore", GITIGNORE))
        if (hasSrc) out.add(Entry("src/utils.rin", utilsModule()))
        if (hasTests) out.add(Entry("tests/test_main.rin", testMain()))
        if (layout.docs) out.add(Entry("docs/overview.md", overviewDoc()))
        if (layout.assets) out.add(Entry("assets/.gitkeep", ""))
        return out
    }

    /** المسارات الأساسية التي يولّدها [ProjectManager.createProject] لهذا النوع (للمعاينة فقط). */
    fun basePaths(): List<String> = when (typeId) {
        "html_js" -> listOf("main.rin", "index.html", "style.css", "script.js", "container.rin")
        "html" -> listOf("main.rin", "index.html", "style.css", "container.rin")
        else -> listOf("main.rin", "indsin/main.indsin")
    }

    /** كل مسارات المشروع المتوقعة (الأساسية + المخطَّطة) للمعاينة الشجرية. */
    fun previewPaths(): List<String> = basePaths() + plan().map { if (it.dir) it.path.trimEnd('/') + "/" else it.path }

    // ------------------------------------------------------------------ main.rin

    /** كتلة الاستيراد (مكتبات + وحدة src) كنص يُلصق أعلى main.rin؛ فارغة إن لم يوجد شيء. */
    fun importBlock(): String {
        val sb = StringBuilder()
        val libs = libraries.map { it.trim() }.filter { it.isNotEmpty() }.distinct()
        if (libs.isNotEmpty()) {
            sb.append(if (en) "// ── Libraries (add more from the Libraries screen) ──\n" else "// ── المكتبات (يمكن إضافة المزيد من شاشة «المكتبات») ──\n")
            for (l in libs) sb.append("@import \"").append(l.replace("\"", "")).append("\";\n")
            sb.append('\n')
        }
        if (hasSrc) {
            sb.append(if (en) "// ── Project modules ──\n" else "// ── وحدات المشروع ──\n")
            sb.append("@import \"./src/utils.rin\";\n\n")
        }
        return sb.toString()
    }

    /** main.rin النهائي: كتلة الاستيراد أولاً ثم القالب الأصلي. idempotent (لا تُكرَّر الكتلة). */
    fun patchMain(original: String): String {
        val block = importBlock()
        if (block.isEmpty() || original.contains(block.trim())) return original
        return block + original
    }

    // ------------------------------------------------------------------ محتوى الملفات

    private fun utilsModule(): String =
        if (en)
            "// src/utils.rin — shared helpers for \"$name\". Only `export`ed names are visible to importers.\n\n" +
                "export fun clampTo(x, lo, hi) {\n    if (x < lo) { return lo; }\n    if (x > hi) { return hi; }\n    return x;\n}\n\n" +
                "export fun greet(who) { return \"Hello, \" + who + \"!\"; }\n\n" +
                "export let APP_NAME = \"${escapeRin(name)}\";\n"
        else
            "// src/utils.rin — دوال مساعدة مشتركة لمشروع \"$name\". ما يُصدَّر بـ export فقط يراه المستورِد.\n\n" +
                "export fun clampTo(x, lo, hi) {\n    if (x < lo) { return lo; }\n    if (x > hi) { return hi; }\n    return x;\n}\n\n" +
                "export fun greet(who) { return \"مرحباً \" + who + \"!\"; }\n\n" +
                "export let APP_NAME = \"${escapeRin(name)}\";\n"

    private fun testMain(): String =
        (if (en) "// tests/test_main.rin — run with: rin test tests   (a failed assert fails the file)\n"
        else "// tests/test_main.rin — شغّلها بالأمر: rin test tests   (فشل assert يُفشل الملف)\n") +
            "@import \"../src/utils.rin\";\n\n" +
            "assert(clampTo(5, 0, 1) == 1, \"clampTo upper bound\");\n" +
            "assert(clampTo(-3, 0, 1) == 0, \"clampTo lower bound\");\n" +
            "assert(clampTo(0.5, 0, 1) == 0.5, \"clampTo inside range\");\n" +
            "assert(APP_NAME == \"${escapeRin(name)}\", \"APP_NAME\");\n" +
            "print \"ok\";\n"

    private fun readme(): String {
        val desc = description.trim().ifEmpty { if (en) "A Rin project." else "مشروع Rin." }
        val libs = libraries.map { it.trim() }.filter { it.isNotEmpty() }.distinct()
        val sb = StringBuilder()
        sb.append("# ").append(name).append("\n\n").append(desc).append("\n\n")
        if (author.isNotBlank()) sb.append(if (en) "**Author:** " else "**المطوّر:** ").append(author.trim()).append("  \n")
        sb.append(if (en) "**Version:** " else "**الإصدار:** ").append(version).append("  \n")
        sb.append(if (en) "**Type:** " else "**النوع:** ").append(typeId).append("\n\n")
        sb.append(if (en) "## Layout\n\n" else "## البنية\n\n")
        sb.append("| ").append(if (en) "Path" else "المسار").append(" | ").append(if (en) "Purpose" else "الغرض").append(" |\n|---|---|\n")
        sb.append("| `main.rin` | ").append(if (en) "entry point" else "نقطة الدخول").append(" |\n")
        if (hasSrc) sb.append("| `src/` | ").append(if (en) "shared modules (`export fun …`)" else "وحدات مشتركة (`export fun …`)").append(" |\n")
        if (hasTests) sb.append("| `tests/` | ").append(if (en) "assert-based tests" else "اختبارات assert").append(" |\n")
        if (layout.docs) sb.append("| `docs/` | ").append(if (en) "documentation" else "التوثيق").append(" |\n")
        if (layout.assets) sb.append("| `assets/` | ").append(if (en) "images, fonts, data" else "صور وخطوط وبيانات").append(" |\n")
        if (libs.isNotEmpty()) {
            sb.append("\n").append(if (en) "## Libraries\n\n" else "## المكتبات\n\n")
            for (l in libs) sb.append("- `").append(l).append("`\n")
        }
        if (hasTests) sb.append("\n").append(if (en) "## Tests\n\n" else "## الاختبارات\n\n").append("```\nrin test tests\n```\n")
        return sb.toString()
    }

    private fun overviewDoc(): String =
        if (en) "# $name — overview\n\n## Goals\n\n- \n\n## Architecture\n\n- `main.rin` wires the pieces together.\n" +
            (if (hasSrc) "- `src/` holds reusable modules.\n" else "") + "\n## Decisions\n\n- \n"
        else "# $name — نظرة عامة\n\n## الأهداف\n\n- \n\n## البنية\n\n- `main.rin` يربط الأجزاء.\n" +
            (if (hasSrc) "- `src/` للوحدات القابلة لإعادة الاستخدام.\n" else "") + "\n## قرارات التصميم\n\n- \n"

    private fun manifestJson(createdAt: String): String {
        val libs = libraries.map { it.trim() }.filter { it.isNotEmpty() }.distinct()
        val sb = StringBuilder("{\n")
        sb.append("  \"name\": ").append(jsonString(name)).append(",\n")
        sb.append("  \"version\": ").append(jsonString(version)).append(",\n")
        sb.append("  \"description\": ").append(jsonString(description.trim())).append(",\n")
        sb.append("  \"author\": ").append(jsonString(author.trim())).append(",\n")
        sb.append("  \"type\": ").append(jsonString(typeId)).append(",\n")
        sb.append("  \"main\": \"main.rin\",\n")
        if (createdAt.isNotEmpty()) sb.append("  \"created\": ").append(jsonString(createdAt)).append(",\n")
        sb.append("  \"libraries\": [")
        sb.append(libs.joinToString(", ") { jsonString(it) })
        sb.append("]\n}\n")
        return sb.toString()
    }

    companion object {
        const val GITIGNORE = "# Rin\nbuild/\n*.rbc\n.rin-cache/\n\n# OS / editors\n.DS_Store\nThumbs.db\n*.swp\n.idea/\n.vscode/\n"

        fun jsonString(s: String): String {
            val sb = StringBuilder("\"")
            for (c in s) when (c) {
                '"' -> sb.append("\\\""); '\\' -> sb.append("\\\\"); '\n' -> sb.append("\\n")
                '\r' -> sb.append("\\r"); '\t' -> sb.append("\\t")
                else -> if (c < ' ') sb.append("\\u%04x".format(c.code)) else sb.append(c)
            }
            return sb.append('"').toString()
        }

        /** يهرّب نصاً لوضعه داخل literal نصي في Rin. */
        fun escapeRin(s: String): String = s.replace("\\", "\\\\").replace("\"", "\\\"")

        /** يرسم شجرة ملفات من مسارات نسبية (المجلد ينتهي بـ "/"): مجلدات أولاً ثم ملفات، كلٌّ أبجدياً. */
        fun renderTree(paths: List<String>): List<String> {
            class Node(val name: String, val dir: Boolean) { val kids = LinkedHashMap<String, Node>() }
            val root = Node("", true)
            for (p in paths) {
                val isDir = p.endsWith("/")
                val parts = p.trimEnd('/').split('/').filter { it.isNotEmpty() }
                var cur = root
                for ((i, part) in parts.withIndex()) {
                    val last = i == parts.lastIndex
                    cur = cur.kids.getOrPut(part) { Node(part, !last || isDir) }
                }
            }
            val lines = ArrayList<String>()
            fun walk(n: Node, prefix: String) {
                val kids = n.kids.values.sortedWith(compareBy({ !it.dir }, { it.name.lowercase() }))
                for ((i, k) in kids.withIndex()) {
                    val last = i == kids.lastIndex
                    lines.add(prefix + (if (last) "└── " else "├── ") + k.name + (if (k.dir) "/" else ""))
                    if (k.dir) walk(k, prefix + (if (last) "    " else "│   "))
                }
            }
            walk(root, "")
            return lines
        }

        /** اسم صالح لمجلد مشروع (نفس منطق [ProjectManager.isValidProjectName] تقريباً لكن مع سبب الرفض). */
        fun nameProblem(raw: String): String? {
            val n = raw.trim()
            if (n.isEmpty()) return "empty"
            if (n.length > 64) return "long"
            if (n.any { it in "/\\:*?\"<>|" } || n == "." || n == ".." || n.startsWith(".")) return "chars"
            return null
        }
    }
}
