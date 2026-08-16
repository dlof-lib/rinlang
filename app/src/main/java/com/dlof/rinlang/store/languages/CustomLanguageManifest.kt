package com.dlof.rinlang.store.languages

import org.json.JSONObject
import java.io.File

/**
 * وصف "مشروع لغة مخصصة" (Custom Language Project) — مشروع Rin عادي (راجع [com.dlof.rinlang.ProjectManager])
 * يحتوي بالإضافة لملفاته العادية على manifest.json يصفه كلغة برمجة كاملة: Lexer.rin/Parser.rin/
 * Interpreter.rin/CodeGen.rin/run.rin + syntax.rinsyntax.json للتلوين. راجع templates/customlang/
 * للقالب الأصلي الذي يُنسَخ منه كل مشروع جديد (عبر [CustomLanguageProjectScaffolder]).
 *
 * هذا النوع مستقل عن [com.dlof.rinlang.store.extensions.RinExtension] عمداً: بينما RinExtension هو
 * تمثيل "إضافة منشورة في المتجر"، فـCustomLanguageManifest هو تمثيل "مشروع لغة محلي على القرص" —
 * النشر (راجع [LanguageMarketplacePublisher]) هو ما يحوّل الثاني إلى الأول عبر
 * type = ExtensionType.LANGUAGE.id.
 */
data class CustomLanguageManifest(
    val id: String = "",
    val name: String = "",
    val version: String = "0.1.0",
    val developer: String = "",
    val fileExtension: String = "",
    val description: String = "",
    val lexerEntry: String = "Lexer.rin",
    val parserEntry: String = "Parser.rin",
    val interpreterEntry: String = "Interpreter.rin",
    val codegenEntry: String = "CodeGen.rin",
    val runEntry: String = "run.rin",
    val syntaxFile: String = "syntax.rinsyntax.json",
    /** شارة محلية فقط للعرض؛ الحالة "الرسمية" الحقيقية تعيش في RinExtension.isOfficial بعد النشر والمراجعة. */
    val official: Boolean = false
) {
    companion object {
        const val MANIFEST_FILE_NAME = "manifest.json"

        fun fromJson(json: JSONObject): CustomLanguageManifest {
            val entry = json.optJSONObject("entry") ?: JSONObject()
            return CustomLanguageManifest(
                id = json.optString("id"),
                name = json.optString("name"),
                version = json.optString("version", "0.1.0"),
                developer = json.optString("developer"),
                fileExtension = json.optString("fileExtension"),
                description = json.optString("description"),
                lexerEntry = entry.optString("lexer", "Lexer.rin"),
                parserEntry = entry.optString("parser", "Parser.rin"),
                interpreterEntry = entry.optString("interpreter", "Interpreter.rin"),
                codegenEntry = entry.optString("codegen", "CodeGen.rin"),
                runEntry = entry.optString("run", "run.rin"),
                syntaxFile = json.optString("syntax", "syntax.rinsyntax.json"),
                official = json.optBoolean("official", false)
            )
        }

        /** يقرأ manifest.json من مجلد مشروع؛ يُعيد null إن لم يكن هذا المشروع مشروع لغة مخصصة أصلاً. */
        fun read(projectDir: File): CustomLanguageManifest? {
            val file = File(projectDir, MANIFEST_FILE_NAME)
            if (!file.exists()) return null
            return try {
                fromJson(JSONObject(file.readText(Charsets.UTF_8)))
            } catch (t: Throwable) {
                null
            }
        }
    }

    fun toJson(): JSONObject = JSONObject().apply {
        put("id", id)
        put("name", name)
        put("version", version)
        put("developer", developer)
        put("fileExtension", fileExtension)
        put("description", description)
        put("entry", JSONObject().apply {
            put("lexer", lexerEntry)
            put("parser", parserEntry)
            put("interpreter", interpreterEntry)
            put("codegen", codegenEntry)
            put("run", runEntry)
        })
        put("syntax", syntaxFile)
        put("official", official)
        put("createdWithRinLangVersion", "langkit-1.0")
    }

    fun write(projectDir: File) {
        File(projectDir, MANIFEST_FILE_NAME).writeText(toJson().toString(2), Charsets.UTF_8)
    }
}
