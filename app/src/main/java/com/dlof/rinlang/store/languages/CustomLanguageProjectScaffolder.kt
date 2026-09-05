package com.dlof.rinlang.store.languages

import android.content.Context
import com.dlof.rinlang.Project
import com.dlof.rinlang.ProjectManager
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

    /**
     * يثبّت لغة "Illust" المضمَّنة (راجع [BundledIllustLanguage]) داخل مشروع أُنشئ حديثاً:
     * يحذف main.rin الترحيبي، يكتب كل ملفات اللغة الجاهزة والمُختبرة كما هي (بلا استبدال
     * عناصر نائبة، فهي محتوى حقيقي ثابت وليست قالباً فارغاً)، ثم يسجّلها في
     * [CustomLanguageRegistry] حتى تُلوَّن ملفات .illust فوراً من أول فتح.
     */
    fun installBundledIllust(context: Context, projectDir: File) {
        File(projectDir, "main.rin").delete()

        File(projectDir, "Lexer.rin").writeText(BundledIllustLanguage.LEXER_RIN, Charsets.UTF_8)
        File(projectDir, "Parser.rin").writeText(BundledIllustLanguage.PARSER_RIN, Charsets.UTF_8)
        File(projectDir, "Interpreter.rin").writeText(BundledIllustLanguage.INTERPRETER_RIN, Charsets.UTF_8)
        File(projectDir, "CodeGen.rin").writeText(BundledIllustLanguage.CODEGEN_RIN, Charsets.UTF_8)
        File(projectDir, "run.rin").writeText(BundledIllustLanguage.RUN_RIN, Charsets.UTF_8)
        File(projectDir, "syntax.rinsyntax.json").writeText(BundledIllustLanguage.SYNTAX_JSON, Charsets.UTF_8)
        File(projectDir, "README.md").writeText(BundledIllustLanguage.README_MD, Charsets.UTF_8)

        val examplesDir = File(projectDir, "examples").apply { mkdirs() }
        File(examplesDir, "hello.illust").writeText(BundledIllustLanguage.EXAMPLE_HELLO_ILLUST, Charsets.UTF_8)

        val manifest = CustomLanguageManifest(
            id = BundledIllustLanguage.LANGUAGE_ID,
            name = BundledIllustLanguage.LANGUAGE_NAME,
            developer = BundledIllustLanguage.DEVELOPER,
            fileExtension = BundledIllustLanguage.FILE_EXTENSION,
            description = BundledIllustLanguage.DESCRIPTION
        )
        manifest.write(projectDir)

        CustomLanguageRegistry.register(context, manifest, projectDir)
    }
}
