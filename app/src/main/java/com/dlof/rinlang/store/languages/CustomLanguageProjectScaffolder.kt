package com.dlof.rinlang.store.languages

import android.content.Context
import com.dlof.rinlang.Project
import com.dlof.rinlang.ProjectManager
import com.dlof.rinlang.packs.PackStore
import java.io.File

/**
 * ينشئ "مشروع لغة مخصصة" جديداً: مشروع Rin عادي (نفس [ProjectManager.createProject]، فيحصل على
 * basePath خاص به كأي مشروع) لكن بدل main.rin ترحيبي، يُنسخ فيه قالب لغة كامل — Lexer.rin/
 * Parser.rin/Interpreter.rin/CodeGen.rin/run.rin/manifest.json/syntax.rinsyntax.json/README.md —
 * من [CustomLanguageTemplates]، مع استبدال العناصر النائبة باسم/معرّف/امتداد اللغة الذي اختاره
 * المستخدم. النتيجة مشروع يعمل فوراً (شغّل run.rin) ويمكن تطويره كأي كود Rin عادي من المحرر.
 */
object CustomLanguageProjectScaffolder {

    /** أسماء لغات صالحة: نفس قيود [ProjectManager.isValidProjectName] + قواعد معرّف/امتداد إضافية. */
    fun isValidLanguageId(id: String): Boolean =
        id.trim().isNotEmpty() && id.trim().matches(Regex("^[a-z][a-z0-9_]{1,31}$"))

    fun isValidFileExtension(ext: String): Boolean =
        ext.trim().isNotEmpty() && ext.trim().matches(Regex("^[a-z][a-z0-9]{0,15}$"))

    /**
     * ينشئ مشروع اللغة الجديد. يرمي IllegalArgumentException لو الاسم/المعرّف/الامتداد غير
     * صالح، أو لو يوجد مشروع بنفس الاسم مسبقاً (يُفوَّض التحقق الأخير لـ[ProjectManager]).
     */
    fun createLanguageProject(
        context: Context,
        projectName: String,
        languageId: String,
        languageName: String,
        fileExtension: String,
        developer: String,
        description: String
    ): Project {
        require(isValidLanguageId(languageId)) {
            "معرّف اللغة غير صالح: يجب أن يبدأ بحرف صغير ويحتوي حروفاً/أرقاماً/شرطة سفلية فقط"
        }
        require(isValidFileExtension(fileExtension)) {
            "امتداد الملف غير صالح: مثال calc أو mylang، حروف/أرقام إنجليزية صغيرة فقط بلا نقطة"
        }
        require(languageName.trim().isNotEmpty()) { "اسم اللغة مطلوب" }

        // يُنشئ المجلد + main.rin ترحيبي أولاً عبر ProjectManager (يضمن basePath صحيح وتفرّد الاسم)
        val project = ProjectManager.createProject(context, projectName)
        val dir = project.dir

        // نستبدل main.rin الترحيبي بملفات مشروع اللغة الفعلية
        File(dir, "main.rin").delete()

        fun render(template: String): String = template
            .replace("__LANG_ID__", languageId.trim())
            .replace("__LANG_NAME__", languageName.trim())
            .replace("__LANG_EXT__", fileExtension.trim())
            .replace("__DEVELOPER__", developer.trim().ifEmpty { "مطوّر مجهول" })
            .replace("__LANG_DESCRIPTION__", description.trim().ifEmpty { "لغة برمجة مخصصة مبنية فوق Rin و langkit.og.rin" })

        File(dir, "Lexer.rin").writeText(render(CustomLanguageTemplates.LEXER_TEMPLATE), Charsets.UTF_8)
        File(dir, "Parser.rin").writeText(render(CustomLanguageTemplates.PARSER_TEMPLATE), Charsets.UTF_8)
        File(dir, "Interpreter.rin").writeText(render(CustomLanguageTemplates.INTERPRETER_TEMPLATE), Charsets.UTF_8)
        File(dir, "CodeGen.rin").writeText(render(CustomLanguageTemplates.CODEGEN_TEMPLATE), Charsets.UTF_8)
        File(dir, "run.rin").writeText(render(CustomLanguageTemplates.RUN_TEMPLATE), Charsets.UTF_8)
        File(dir, "syntax.rinsyntax.json").writeText(render(CustomLanguageTemplates.SYNTAX_TEMPLATE), Charsets.UTF_8)
        File(dir, "README.md").writeText(render(CustomLanguageTemplates.README_TEMPLATE), Charsets.UTF_8)

        val examplesDir = File(dir, "examples").apply { mkdirs() }
        File(examplesDir, "hello.${fileExtension.trim()}").writeText(
            "// أول برنامج بلغتك ${languageName.trim()}\n" +
                "let x = 1 + 2;\n" +
                "print x;\n",
            Charsets.UTF_8
        )

        val manifest = CustomLanguageManifest(
            id = languageId.trim(),
            name = languageName.trim(),
            developer = developer.trim().ifEmpty { "مطوّر مجهول" },
            fileExtension = fileExtension.trim(),
            description = description.trim().ifEmpty { "لغة برمجة مخصصة مبنية فوق Rin و langkit.og.rin" }
        )
        manifest.write(dir)

        CustomLanguageRegistry.register(context, manifest, dir)

        return project
    }

    /** معرّف حزمة الأصول التي تحمل ملفات لغة Illust (انظر content-packs/packs.json). */
    const val ILLUST_PACK_ID = "illust-lang"

    private const val ILLUST_LANGUAGE_ID = "illust"
    private const val ILLUST_LANGUAGE_NAME = "Illust"
    private const val ILLUST_FILE_EXTENSION = "illust"
    private const val ILLUST_DEVELOPER = "Rin Team"
    private const val ILLUST_DESCRIPTION =
        "لغة رسم/جرافيكس صغيرة فوق Rin: أوامر نصية (canvas/rect/circle/ellipse/polygon/path/line/text/fill/stroke/group/rotate) " +
            "مع متغيرات وشروط وحلقات ودوال قابلة لإعادة الاستخدام، تتحول لمخرجات SVG حقيقية."

    private val ILLUST_FILES = listOf(
        "Lexer.rin", "Parser.rin", "Interpreter.rin", "CodeGen.rin", "run.rin", "syntax.rinsyntax.json", "README.md"
    )

    /**
     * يثبّت لغة "Illust" داخل مشروع أُنشئ حديثاً من ملفات حزمة [ILLUST_PACK_ID] المنزَّلة (لم تعد مضمَّنة
     * كسلاسل Kotlin داخل التطبيق لتقليل حجمه): يحذف main.rin الترحيبي، ينسخ ملفات اللغة الجاهزة والمُختبرة كما هي،
     * ثم يسجّلها في [CustomLanguageRegistry] حتى تُلوَّن ملفات .illust فوراً من أول فتح.
     *
     * يجب أن تكون الحزمة مثبّتة قبل الاستدعاء (استعمل PackDownloadDialog.ensure)، وإلا يُرمى [IllegalStateException].
     *
     * [includeExample] يتحكّم بمحتوى examples/: true (الافتراضي) يثبّت مثال hello.illust الجاهز؛
     * false يثبّت بدلاً منه canvas.illust فارغاً (تعليق توضيحي فقط).
     */
    fun installBundledIllust(context: Context, projectDir: File, includeExample: Boolean = true) {
        if (!PackStore.isInstalled(context, ILLUST_PACK_ID)) {
            throw IllegalStateException("Illust pack ($ILLUST_PACK_ID) is not installed")
        }
        val pack = PackStore.dirOf(context, ILLUST_PACK_ID)

        File(projectDir, "main.rin").delete()
        for (name in ILLUST_FILES) {
            File(pack, name).copyTo(File(projectDir, name), overwrite = true)
        }

        val examplesDir = File(projectDir, "examples").apply { mkdirs() }
        val hello = File(pack, "examples/hello.illust")
        if (includeExample && hello.isFile) {
            hello.copyTo(File(examplesDir, "hello.illust"), overwrite = true)
        } else {
            File(examplesDir, "canvas.illust").writeText(
                "// لوحة رسم فارغة — ابدأ الرسم هنا بلغة $ILLUST_LANGUAGE_NAME\n",
                Charsets.UTF_8
            )
        }

        val manifest = CustomLanguageManifest(
            id = ILLUST_LANGUAGE_ID,
            name = ILLUST_LANGUAGE_NAME,
            developer = ILLUST_DEVELOPER,
            fileExtension = ILLUST_FILE_EXTENSION,
            description = ILLUST_DESCRIPTION
        )
        manifest.write(projectDir)

        CustomLanguageRegistry.register(context, manifest, projectDir)
    }
}
