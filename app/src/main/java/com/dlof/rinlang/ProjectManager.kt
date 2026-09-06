package com.dlof.rinlang

import android.content.ContentResolver
import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import java.io.BufferedOutputStream
import java.io.File
import java.io.FileOutputStream
import java.util.zip.ZipEntry
import java.util.zip.ZipInputStream
import java.util.zip.ZipOutputStream

/**
 * يدير مشاريع Rin على تخزين التطبيق الخاص (filesDir/projects/<name>/...).
 * كل مشروع مجلد مستقل، حتى يحصل على basePath خاص به عند تمريره لـ RinEngine —
 * فتبقى عمليات save/installation/file (انظر لغة الحاويات في README) معزولة
 * بين مشروع وآخر بدل أن تتشارك كلها rin_installed/ واحدة.
 *
 * "رفع ملف" (upload) هنا يعني: استيراد ملف موجود بالفعل على الجهاز (أو Google
 * Drive وغيرها عبر SAF) بنسخ محتواه داخل مجلد المشروع، تماماً كما يفعل زر
 * "فتح" في MainActivity لكن مع الاحتفاظ بنسخة دائمة داخل المشروع بدل مجرد
 * فتحها في المحرر لمرة واحدة.
 */
object ProjectManager {

    private const val PROJECTS_DIR = "projects"
    private const val RIN_EXTENSION = ".rin"

    /**
     * خيارات "رسم الواجهة" لمشروع نوع UI، تُختار في حوار "مشروع جديد" (انظر
     * ProjectsActivity.showCreateDialog) قبل الإنشاء: توب بار/بلا توب بار، قائمة جانبية/بلا
     * قائمة جانبية، ولون أساسي (primary) يُكتب داخل @theme في main.rin المولَّد باستخدام
     * محرّك الواجهات Loomtime (@view.Scaffold/@view.TopBar/@view.Drawer، انظر
     * docs/loomtime/RIN_LOOM_ENGINE_ARCHITECTURE.md وsite.rin). لا تؤثر على أنواع المشاريع
     * الأخرى (Container/Table/Free)، وتُستخدم قيمها الافتراضية عند عدم تمريرها صراحةً.
     */
    data class UiDesignOptions(
        val topBar: Boolean = true,
        val sidebar: Boolean = true,
        val bottomNav: Boolean = false,
        val buttonStyle: String = "filled",
        val primaryColor: String = "#7C5CFF",
        val background: String = "#0F0F14",
        val text: String = "#F5F5F7",
        val fontFamily: String = "sans",
        val typography: String = "medium",
        val cornerRadius: Int = 14
    )

    /**
     * ملف البيانات الوصفية لمشروع واحد، في جذر مجلد المشروع مباشرة. يحفظ حالياً نوع المشروع
     * (container/table/ui/free) الذي اختاره المستخدم عند الإنشاء، داخل حاوية Rin مصغّرة
     * (@container ... .end/container) بدل سطر "مفتاح=قيمة" خام — تماشياً مع لغة Rin نفسها
     * ونفس البنية المستخدمة في main.rin. يتنقّل مع المشروع تلقائياً عند إعادة التسمية لأنه
     * مجرد ملف داخل نفس المجلد. القراءة تبقى متوافقة مع المشاريع القديمة التي كُتب فيها هذا
     * الملف بصيغة "type=xxx" الخام قبل هذا التعديل.
     */
    private const val PROJECT_META_FILE = "project.og.urin"

    private fun projectsRoot(context: Context): File {
        val root = File(context.filesDir, PROJECTS_DIR)
        if (!root.exists()) root.mkdirs()
        return root
    }

    /** أسماء المشاريع الصالحة: حروف/أرقام/شرطة/شرطة سفلية فقط، لتفادي مشاكل مسارات الملفات. */
    fun isValidProjectName(name: String): Boolean {
        val trimmed = name.trim()
        return trimmed.isNotEmpty() && trimmed.matches(Regex("^[A-Za-z0-9_\\-\\u0600-\\u06FF ]{1,64}$"))
    }

    fun listProjects(context: Context): List<Project> {
        val root = projectsRoot(context)
        return (root.listFiles { f -> f.isDirectory } ?: emptyArray())
            .map { Project(it.name, it, it.lastModified(), readProjectType(it)) }
            .sortedByDescending { it.lastModified }
    }

    /** يلتقط "type=container" (صيغة قديمة خام) أو "type="container";" (صيغة الحاوية الجديدة) سواء بسواء. */
    private val TYPE_LINE_REGEX = Regex("type\\s*=\\s*\"?([A-Za-z0-9_]+)\"?")

    /**
     * يقرأ نوع المشروع من [PROJECT_META_FILE] داخل [dir]، أو FREE لو الملف غائب/تالف (مشاريع
     * أُنشئت قبل هذه الميزة). يعمل مع الصيغتين: الحاوية الجديدة (@container=ProjectMeta ...
     * type="container"; ... .end/container) والصيغة الخام القديمة (type=container سطراً بسطر)،
     * حتى تبقى المشاريع التي أُنشئت قبل التحديث تعمل دون أي هجرة يدوية.
     */
    internal fun readProjectTypeForAlbum(dir: File): ProjectType = readProjectType(dir)

    private fun readProjectType(dir: File): ProjectType {
        val metaFile = File(dir, PROJECT_META_FILE)
        if (!metaFile.isFile) return ProjectType.FREE
        val typeId = TYPE_LINE_REGEX.find(metaFile.readText())?.groupValues?.get(1)
        return ProjectType.fromId(typeId)
    }

    /**
     * يكتب/يحدّث [PROJECT_META_FILE] بنوع المشروع الحالي، بصيغة حاوية Rin مصغّرة (@container)
     * بدل سطر key=value خام: البيانات الوصفية للمشروع تُخزَّن الآن هي نفسها داخل حاوية، بنفس
     * منطق main.rin لمشاريع نوع Container.
     */
    private fun writeProjectMeta(dir: File, type: ProjectType) {
        File(dir, PROJECT_META_FILE).writeText(
            "# بيانات وصفية لمشروع Rin — يُدار تلقائياً من التطبيق، لا تُعدّله يدوياً\n" +
                "// نوع المشروع محفوظ داخل حاوية Rin (@container) وليس سطراً خاماً،\n" +
                "// تماشياً مع بنية اللغة نفسها.\n" +
                "@container=ProjectMeta\n" +
                "    type=\"${type.id}\";\n" +
                ".end/container\n"
        )
    }

    /**
     * القالب الابتدائي لملف main.rin حسب نوع المشروع المختار عند الإنشاء. [uiOptions] لا يُقرأ
     * إلا عندما [type] == ProjectType.UI (انظر فرع ProjectType.UI أدناه).
     */
    private fun mainRinTemplateFor(type: ProjectType, name: String, uiOptions: UiDesignOptions = UiDesignOptions()): String = when (type) {
        ProjectType.CONTAINER ->
            "// مشروع: $name\n" +
                "// نوع المشروع: حاوية (@container) — بيانات حية (warp) داخل حاوية مسمّاة.\n\n" +
                "@container=Main\n" +
                "    warp counter = 0;\n\n" +
                "    print \"مرحباً من حاوية مشروع $name\";\n" +
                "    print \"العداد:\", counter;\n" +
                ".end/container\n"

        ProjectType.TABLE ->
            "// مشروع: $name\n" +
                "// نوع المشروع: جدول بيانات (@table)\n\n" +
                "@table=main_table\n" +
                "    row cells=[\"العمود الأول\", \"العمود الثاني\"];\n" +
                "    row cells=[\"قيمة 1\", \"قيمة 2\"];\n" +
                ".end/table\n\n" +
                "print \"جدول مشروع $name جاهز\";\n"

        ProjectType.UI -> buildUiRinTemplate(name, uiOptions)

        ProjectType.FREE ->
            "// مشروع: $name\n" +
                "print \"مرحباً من مشروع $name\";\n"

        // يُستبدَل فوراً بعد الإنشاء عبر CustomLanguageProjectScaffolder.installBundledIllust
        // (انظر ProjectsActivity)؛ هذا المحتوى احتياطي فقط في حال لم يُستدعَ ذلك لأي سبب.
        ProjectType.ILLUST ->
            "// مشروع: $name\n" +
                "// نوع المشروع: Illust (لغة رسم/SVG مبنية فوق Rin)\n" +
                "print \"مرحباً من مشروع $name\";\n"
    }

    /**
     * يبني main.rin لمشروع UI باستخدام محرّك الواجهات Loomtime (@view.Scaffold/@view.TopBar/
     * @view.Drawer، نفس النحو المستخدم في site.rin وdocs/loomtime/RIN_LOOM_ENGINE_ARCHITECTURE.md)،
     * مطابقاً لمخطّط "رسم الواجهة": شريط علوي اختياري (دائرة صورة رمزية + عنوان)، محتوى رئيسي
     * (بطاقتان مؤطّرتان)، وقائمة جانبية اختيارية (زر "القائمة" + عنصرا تنقّل). كلا العنصرين
     * يُدرج أو يُستبعد حسب [options.topBar]/[options.sidebar]، واللون الأساسي المختار يُستخدم
     * في @theme وفي عناصر الهوية (الدائرة/الزر/العنوان) بدل البنفسجي الثابت وحده.
     */
    private fun buildUiRinTemplate(name: String, options: UiDesignOptions): String {
        val sb = StringBuilder()
        sb.append("// مشروع: $name\n")
        sb.append("// نوع المشروع: واجهة مستخدم (Loomtime) — @container يحوي @theme و@view.\n")
        sb.append("// وُلِّد من خيارات UI: topBar=${options.topBar}, sidebar=${options.sidebar}, bottomNav=${options.bottomNav}, buttonStyle=${options.buttonStyle}.\n")
        sb.append("// font=${options.fontFamily}, typography=${options.typography}, radius=${options.cornerRadius}.\n\n")
        sb.append("@container=Home\n")
        sb.append("    warp userName = \"زائر\";\n\n")
        sb.append("    @theme=Midnight\n")
        sb.append("        active=true;\n")
        sb.append("        primary=\"${options.primaryColor}\";\n")
        sb.append("        background=\"${options.background}\";\n")
        sb.append("        text=\"${options.text}\";\n")
        sb.append("        font=\"${options.fontFamily}\"; typography=\"${options.typography}\"; radius=${options.cornerRadius};\n")
        sb.append("    .end/theme\n\n")

        sb.append("    @view.Scaffold=Root\n")

        // الشريط العلوي: دائرة صورة رمزية + عنوان، فقط لو options.topBar == true.
        if (options.topBar) {
            sb.append("        @view.TopBar=nav\n")
            sb.append("            role=\"topbar\"; padding=16; bg=\"${options.background}\";\n")
            sb.append("            @view.Row=navRow\n")
            sb.append("                gap=12; valign=\"center\";\n")
            sb.append("                @view.Card=navAvatar width=44; height=44; radius=999; bg=\"${options.primaryColor}\"; .end/view\n")
            sb.append("                @view.Text=navTitle text=\"شريط علوي\"; size=18; weight=\"800\"; color=\"${options.primaryColor}\"; .end/view\n")
            sb.append("            .end/view\n")
            sb.append("        .end/view\n\n")
        }

        // الجسم: محتوى رئيسي (بطاقتان مؤطّرتان) + قائمة جانبية اختيارية بعرض ثابت.
        sb.append("        @view.Row=body\n")
        sb.append("            gap=0;\n\n")

        sb.append("            @view.Column=content\n")
        sb.append("                grow=1; gap=20; padding=20;\n")
        sb.append("                @view.Card=placeholder1 height=180; radius=${options.cornerRadius}; bg=\"transparent\"; borderColor=\"${options.text}22\"; borderWidth=1; .end/view\n")
        sb.append("                @view.Card=placeholder2 height=180; radius=${options.cornerRadius}; bg=\"transparent\"; borderColor=\"${options.text}22\"; borderWidth=1; .end/view\n")
        sb.append("            .end/view\n")

        val buttonBg = when (options.buttonStyle) {
            "outline" -> "transparent"
            "soft" -> options.primaryColor + "22"
            else -> options.primaryColor
        }
        val buttonBorder = if (options.buttonStyle == "outline") " borderColor=\"${options.primaryColor}\"; borderWidth=1;" else ""

        if (options.sidebar) {
            sb.append("\n            @view.Drawer=sidebar\n")
            sb.append("                role=\"sidebar\"; open=\"true\"; width=220; bg=\"${options.background}\";\n")
            sb.append("                @view.Column=sidebarCol\n")
            sb.append("                    gap=10; padding=16;\n")
            sb.append("                    @view.Button=menuBtn label=\"القائمة\"; bg=\"$buttonBg\"; color=\"${options.primaryColor}\"; radius=${options.cornerRadius};$buttonBorder .end/view\n")
            sb.append("                    @view.MenuItem=sidebarItem1 label=\"\"; bg=\"${options.text}14\"; radius=10; height=34; .end/view\n")
            sb.append("                    @view.MenuItem=sidebarItem2 label=\"\"; bg=\"${options.text}14\"; radius=10; height=34; .end/view\n")
            sb.append("                .end/view\n")
            sb.append("            .end/view\n")
        }

        if (options.bottomNav) {
            sb.append("\n        @view.BottomBar=bottomNav\n")
            sb.append("            height=64; bg=\"${options.background}\";\n")
            sb.append("            @view.Row=bottomItems gap=18; valign=\"center\";\n")
            sb.append("                @view.Button=home label=\"الرئيسية\"; bg=\"$buttonBg\"; color=\"${options.primaryColor}\"; radius=${options.cornerRadius};$buttonBorder .end/view\n")
            sb.append("                @view.Button=settings label=\"الإعدادات\"; bg=\"$buttonBg\"; color=\"${options.primaryColor}\"; radius=${options.cornerRadius};$buttonBorder .end/view\n")
            sb.append("            .end/view\n")
            sb.append("        .end/view\n")
        }

        sb.append("        .end/view\n")
        sb.append("    .end/view\n")
        sb.append(".end/container\n")
        return sb.toString()
    }

    /**
     * ينشئ مشروعاً جديداً بمجلد + ملف main.rin ابتدائي مناسب لـ [type] + ملف بيانات وصفية
     * يحفظ هذا النوع، ويرمي IllegalArgumentException لو الاسم مستخدم أو غير صالح.
     */
    fun createProject(
        context: Context,
        name: String,
        type: ProjectType = ProjectType.FREE,
        uiOptions: UiDesignOptions = UiDesignOptions()
    ): Project {
        val trimmed = name.trim()
        require(isValidProjectName(trimmed)) { "اسم المشروع غير صالح" }
        val dir = File(projectsRoot(context), trimmed)
        require(!dir.exists()) { "يوجد مشروع بهذا الاسم بالفعل" }
        dir.mkdirs()
        writeProjectMeta(dir, type)
        File(dir, "main.rin").writeText(mainRinTemplateFor(type, trimmed, uiOptions))
        return Project(trimmed, dir, dir.lastModified(), type)
    }

    fun deleteProject(project: Project): Boolean = project.dir.deleteRecursively()

    fun renameProject(project: Project, newName: String): Project {
        require(isValidProjectName(newName)) { "اسم المشروع غير صالح" }
        val newDir = File(project.dir.parentFile, newName.trim())
        require(!newDir.exists()) { "يوجد مشروع بهذا الاسم بالفعل" }
        val ok = project.dir.renameTo(newDir)
        require(ok) { "تعذّر إعادة تسمية المشروع" }
        // ملف project.og.urin ينتقل تلقائياً مع المجلد، فيبقى النوع كما هو بعد إعادة التسمية.
        return Project(newName.trim(), newDir, newDir.lastModified(), project.type)
    }

    // ---- ملفات داخل مشروع ----

    /**
     * أنواع الملفات "غير النصية" التي لا يصحّ فتحها/تعديلها كنص عادي في المحرر (صور/فيديو/صوت/خطوط/
     * أرشيفات/مستندات ثنائية...). أي امتداد آخر (بما فيه .rin وكل لغات البرمجة الأخرى) يُعامَل كملف
     * نصّي قابل للفتح في [MainActivity] كما كان الحال دائماً.
     */
    private val imageExtensions = setOf("jpg", "jpeg", "png", "webp", "gif", "bmp", "heic", "heif")
    private val videoExtensions = setOf("mp4", "mkv", "webm", "3gp", "mov", "avi", "m4v")
    private val audioExtensions = setOf("mp3", "wav", "ogg", "m4a", "flac", "aac", "opus")
    private val otherBinaryExtensions = setOf(
        "ttf", "otf", "woff", "woff2",
        "zip", "rar", "7z", "apk", "jar",
        "pdf", "doc", "docx", "xls", "xlsx", "ppt", "pptx"
    )

    private fun extensionOf(name: String): String = name.substringAfterLast('.', "").lowercase()

    fun isImageFile(name: String): Boolean = extensionOf(name) in imageExtensions
    fun isVideoFile(name: String): Boolean = extensionOf(name) in videoExtensions
    fun isAudioFile(name: String): Boolean = extensionOf(name) in audioExtensions
    fun isMediaFile(name: String): Boolean = isImageFile(name) || isVideoFile(name) || isAudioFile(name)

    /** true لأي ملف يُفضَّل فتحه بتطبيق خارجي (ACTION_VIEW) بدل معاينته داخل التطبيق أو فتحه كنص. */
    fun isBinaryFile(name: String): Boolean =
        isImageFile(name) || isVideoFile(name) || isAudioFile(name) || extensionOf(name) in otherBinaryExtensions

    /**
     * كل ملفات المشروع مباشرة داخل مجلده (بدون تنقيب في مجلدات فرعية مثل lib/ أو rin_installed/):
     * ملفات .rin كما كان الحال دائماً، بالإضافة إلى أي ملف آخر (صور/فيديو/صوت/خطوط/ملفات لغات
     * برمجة أخرى...) رُفع عبر "رفع ملف" أو "رفع وسائط".
     */
    fun listFiles(project: Project): List<RinFile> = listEntries(project, "").second

    /** حل مجلد نسبي [relDir] (فواصل "/"، فارغ = جذر المشروع) إلى [File] فعلي داخل مجلد المشروع. */
    fun resolveDir(project: Project, relDir: String): File {
        if (relDir.isBlank()) return project.dir
        var dir = project.dir
        for (segment in relDir.split('/').filter { it.isNotBlank() }) dir = File(dir, segment)
        return dir
    }

    /**
     * المجلدات والملفات الموجودة مباشرة (مستوى واحد فقط) داخل [relDir] (فارغ = جذر المشروع)،
     * لدعم التصفّح داخل مجلدات فرعية تُنشأ يدوياً أو عبر "/" في اسم ملف جديد أو فك أرشيف ZIP.
     */
    fun listEntries(project: Project, relDir: String): Pair<List<RinFolder>, List<RinFile>> {
        val dir = resolveDir(project, relDir)
        val prefix = if (relDir.isBlank()) "" else "${relDir.trim('/')}/"
        val children = dir.listFiles() ?: emptyArray()
        val folders = children.filter { it.isDirectory }
            .map { RinFolder(it.name, "$prefix${it.name}", it, it.lastModified()) }
            .sortedByDescending { it.lastModified }
        val files = children.filter { it.isFile }
            .map { RinFile(it.name, it, it.length(), it.lastModified(), "$prefix${it.name}") }
            .sortedByDescending { it.lastModified }
        return folders to files
    }

    fun readFile(rinFile: RinFile): String = rinFile.file.readText()

    /**
     * ينشئ ملفاً جديداً داخل [relDir] (فارغ = جذر المشروع). يقبل [fileName] بفواصل "/" لإنشاء
     * مجلدات فرعية تلقائياً قبل الملف (مثل "ui/screens/home.rin")، تماماً كما لو ضُغط زر
     * "مجلد جديد" لكل مستوى ثم أُنشئ الملف بداخله.
     */
    fun writeFile(project: Project, relDir: String, fileName: String, content: String): RinFile {
        val segments = fileName.trim().split('/').filter { it.isNotBlank() }
        require(segments.isNotEmpty()) { "اسم الملف غير صالح" }
        var dir = resolveDir(project, relDir)
        for (segment in segments.dropLast(1)) {
            dir = File(dir, segment)
            dir.mkdirs()
        }
        val safeName = ensureRinExtension(segments.last())
        val target = File(dir, safeName)
        target.writeText(content)
        val prefix = if (relDir.isBlank()) "" else "${relDir.trim('/')}/"
        val subDirPrefix = segments.dropLast(1).joinToString("") { "$it/" }
        return RinFile(safeName, target, target.length(), target.lastModified(), "$prefix$subDirPrefix$safeName")
    }

    /** إبقاءً على التوافق مع أي استدعاء قديم بلا [relDir] (يُنشئ الملف في جذر المشروع مباشرة). */
    fun writeFile(project: Project, fileName: String, content: String): RinFile =
        writeFile(project, "", fileName, content)

    fun deleteFile(rinFile: RinFile): Boolean = rinFile.file.delete()

    /** اسم مجلد/ملف صالح لمقطع مسار واحد: حروف/أرقام/شرطة/شرطة سفلية/عربي فقط. */
    fun isValidPathSegment(name: String): Boolean {
        val trimmed = name.trim()
        return trimmed.isNotEmpty() && trimmed.matches(Regex("^[A-Za-z0-9_\\-.\\u0600-\\u06FF ]{1,64}$"))
    }

    /** ينشئ مجلداً فرعياً جديداً داخل [relDir] (فارغ = جذر المشروع). */
    fun createFolder(project: Project, relDir: String, name: String): RinFolder {
        val trimmed = name.trim()
        require(isValidPathSegment(trimmed)) { "اسم المجلد غير صالح" }
        val parent = resolveDir(project, relDir)
        val target = File(parent, trimmed)
        require(!target.exists()) { "يوجد ملف أو مجلد بهذا الاسم بالفعل" }
        target.mkdirs()
        val prefix = if (relDir.isBlank()) "" else "${relDir.trim('/')}/"
        return RinFolder(trimmed, "$prefix$trimmed", target, target.lastModified())
    }

    fun deleteFolder(folder: RinFolder): Boolean = folder.dir.deleteRecursively()

    /**
     * يعيد تسمية ملف داخل مجلده الحالي (لا ينقله بين مجلدات). إن كتب المستخدم اسماً بلا امتداد
     * (مثل "notes" بدل "notes.rin")، يُبقي على الامتداد الأصلي للملف تلقائياً.
     */
    fun renameFile(project: Project, rinFile: RinFile, newName: String): RinFile {
        val trimmed = newName.trim()
        require(isValidPathSegment(trimmed)) { "اسم الملف غير صالح" }
        val finalName = if ('.' in trimmed) trimmed else {
            val dot = rinFile.name.lastIndexOf('.')
            if (dot > 0) "$trimmed${rinFile.name.substring(dot)}" else trimmed
        }
        val parentDir = rinFile.file.parentFile ?: project.dir
        val target = File(parentDir, finalName)
        require(!target.exists()) { "يوجد ملف بهذا الاسم بالفعل" }
        val ok = rinFile.file.renameTo(target)
        require(ok) { "تعذّر إعادة تسمية الملف" }
        val parentRelPath = rinFile.relPath.substringBeforeLast('/', "")
        val newRelPath = if (parentRelPath.isEmpty()) finalName else "$parentRelPath/$finalName"
        return RinFile(finalName, target, target.length(), target.lastModified(), newRelPath)
    }

    /** يعيد تسمية مجلد فرعي داخل مكانه الحالي (لا ينقله). */
    fun renameFolder(folder: RinFolder, newName: String): RinFolder {
        val trimmed = newName.trim()
        require(isValidPathSegment(trimmed)) { "اسم المجلد غير صالح" }
        val parentDir = folder.dir.parentFile ?: return folder
        val target = File(parentDir, trimmed)
        require(!target.exists()) { "يوجد ملف أو مجلد بهذا الاسم بالفعل" }
        val ok = folder.dir.renameTo(target)
        require(ok) { "تعذّر إعادة تسمية المجلد" }
        val parentRelPath = folder.relPath.substringBeforeLast('/', "")
        val newRelPath = if (parentRelPath.isEmpty()) trimmed else "$parentRelPath/$trimmed"
        return RinFolder(trimmed, newRelPath, target, target.lastModified())
    }

    private fun ensureRinExtension(name: String): String {
        val trimmed = name.trim()
        return if (trimmed.endsWith(RIN_EXTENSION)) trimmed else "$trimmed$RIN_EXTENSION"
    }

    /**
     * "رفع" ملف من الجهاز (أو أي مزوّد SAF: تخزين محلي، Google Drive، معرض الصور/الفيديو...) إلى
     * داخل المشروع. يحتفظ بامتداد الملف الأصلي كما هو (لم يعد يُجبَر على .rin، حتى تُقبل أنواع
     * أخرى: صور، فيديو، صوت، خطوط، ملفات لغات برمجة أخرى...)، وينسخ المحتوى بايتاً بايت (لا كنص
     * UTF-8) حتى لا تتلف الملفات الثنائية (صور/فيديو/صوت) عند الاستيراد.
     */
    fun importFileFromUri(context: Context, project: Project, uri: Uri): RinFile {
        val resolver: ContentResolver = context.contentResolver
        val displayName = queryDisplayName(resolver, uri) ?: uri.lastPathSegment ?: "imported"

        var safeName = sanitizeFileName(displayName)
        if (safeName.isBlank()) safeName = "imported"
        var target = File(project.dir, safeName)
        var counter = 1
        // تفادي الكتابة فوق ملف موجود بنفس الاسم: أضف رقماً متسلسلاً قبل الامتداد (إن وجد).
        while (target.exists()) {
            val dot = safeName.lastIndexOf('.')
            safeName = if (dot > 0) {
                "${safeName.substring(0, dot)}_$counter${safeName.substring(dot)}"
            } else {
                "${safeName}_$counter"
            }
            target = File(project.dir, safeName)
            counter++
        }

        val input = resolver.openInputStream(uri)
            ?: throw IllegalStateException("تعذّرت قراءة الملف المحدد")
        input.use { streamIn ->
            BufferedOutputStream(FileOutputStream(target)).use { streamOut ->
                streamIn.copyTo(streamOut)
            }
        }
        return RinFile(safeName, target, target.length(), target.lastModified())
    }

    private fun sanitizeFileName(name: String): String =
        name.replace(Regex("[^A-Za-z0-9_\\-.\\u0600-\\u06FF]"), "_")

    // ---- مكتبات المشروع (lib/ *.og.rin) ----
    //
    // كل مشروع يملك مجلداً فرعياً lib/ (أسفل basePath الممرَّر لـ RinEngine)، وأي ملف
    // بداخله بامتداد .og.rin هو "مكتبة" يمكن لأي كود في هذا المشروع استيرادها مباشرة عبر
    // @import "lib/<name>.og.rin"; بالضبط بنفس آلية المكتبات القياسية الخمس المدمجة —
    // فرق المستخدم الوحيد هو أنّ مكتباته توجد فعلياً على القرص بدل أن تكون مدمجة في الثنائي.

    private const val LIB_DIR = "lib"
    private const val LIB_EXTENSION = ".og.rin"

    /** مجلد lib/ الخاص بالمشروع، يُنشأ تلقائياً إن لم يكن موجوداً. */
    fun libDir(project: Project): File {
        val dir = File(project.dir, LIB_DIR)
        if (!dir.exists()) dir.mkdirs()
        return dir
    }

    /** اسم مكتبة صالح: حروف/أرقام/شرطة/شرطة سفلية فقط (قبل امتداد .og.rin)، لتفادي مشاكل مسارات الاستيراد. */
    fun isValidLibraryName(name: String): Boolean {
        val base = name.trim().removeSuffix(LIB_EXTENSION)
        return base.isNotEmpty() && base.matches(Regex("^[A-Za-z0-9_\\-]{1,64}$"))
    }

    /** كل مكتبات المشروع (ملفات lib/ *.og.rin)، الأحدث تعديلاً أولاً. */
    fun listLibraries(project: Project): List<RinLibrary> {
        return (libDir(project).listFiles { f -> f.isFile && f.name.endsWith(LIB_EXTENSION) } ?: emptyArray())
            .map { RinLibrary(it.name, it, it.length(), it.lastModified()) }
            .sortedByDescending { it.lastModified }
    }

    fun readLibrary(library: RinLibrary): String = library.file.readText()

    /** يُنشئ مكتبة جديدة (فارغة أو بمحتوى بدائي جاهز) داخل lib/ الخاص بالمشروع. */
    fun createLibrary(project: Project, name: String): RinLibrary {
        val trimmed = name.trim()
        require(isValidLibraryName(trimmed)) { "اسم المكتبة غير صالح (حروف/أرقام/شرطة فقط)" }
        val safeName = ensureLibExtension(trimmed)
        val target = File(libDir(project), safeName)
        require(!target.exists()) { "توجد مكتبة بهذا الاسم بالفعل" }
        val libId = safeName.removeSuffix(LIB_EXTENSION)
        target.writeText(
            "// ============================================================================\n" +
                "//  lib/$safeName — مكتبتك الخاصة\n" +
                "//  استيراد:\n" +
                "//    @import \"lib/$safeName\";\n" +
                "//    @import \"lib/$safeName\" as $libId;\n" +
                "// ============================================================================\n\n" +
                "fun hello() {\n" +
                "    return \"مرحباً من مكتبة $libId\";\n" +
                "}\n"
        )
        return RinLibrary(safeName, target, target.length(), target.lastModified())
    }

    fun writeLibrary(project: Project, library: RinLibrary, content: String): RinLibrary {
        val target = File(libDir(project), library.name)
        target.writeText(content)
        return RinLibrary(library.name, target, target.length(), target.lastModified())
    }

    fun deleteLibrary(library: RinLibrary): Boolean = library.file.delete()

    /** يعيد تسمية مكتبة lib/ *.og.rin، مع فرض امتداد .og.rin دوماً على الاسم الجديد. */
    fun renameLibrary(project: Project, library: RinLibrary, newName: String): RinLibrary {
        val trimmed = newName.trim().removeSuffix(LIB_EXTENSION)
        require(isValidLibraryName(trimmed)) { "اسم المكتبة غير صالح (حروف/أرقام/شرطة فقط)" }
        val safeName = ensureLibExtension(trimmed)
        val target = File(libDir(project), safeName)
        require(!target.exists()) { "توجد مكتبة بهذا الاسم بالفعل" }
        val ok = library.file.renameTo(target)
        require(ok) { "تعذّر إعادة تسمية المكتبة" }
        return RinLibrary(safeName, target, target.length(), target.lastModified())
    }

    private fun ensureLibExtension(name: String): String =
        if (name.endsWith(LIB_EXTENSION)) name else "$name$LIB_EXTENSION"

    /**
     * "رفع" مكتبة من الجهاز (أو أي مزوّد SAF) إلى داخل lib/ الخاص بالمشروع، مثل
     * [importFileFromUri] تماماً لكن للمكتبات: يضمن امتداد .og.rin دوماً حتى لو كان
     * اسم الملف الأصلي على الجهاز مختلفاً (مثل mylib.rin أو mylib.txt).
     */
    fun importLibraryFromUri(context: Context, project: Project, uri: Uri): RinLibrary {
        val resolver: ContentResolver = context.contentResolver
        val displayName = queryDisplayName(resolver, uri) ?: uri.lastPathSegment ?: "imported"
        val text = resolver.openInputStream(uri)?.bufferedReader()?.use { it.readText() }
            ?: throw IllegalStateException("تعذّرت قراءة المكتبة المحددة")

        val baseName = sanitizeFileName(displayName)
            .removeSuffix(".rin").removeSuffix(LIB_EXTENSION).removeSuffix(".txt")
        var safeName = ensureLibExtension(baseName.ifBlank { "library" })
        var target = File(libDir(project), safeName)
        var counter = 1
        while (target.exists()) {
            val base = safeName.removeSuffix(LIB_EXTENSION)
            safeName = "${base}_$counter$LIB_EXTENSION"
            target = File(libDir(project), safeName)
            counter++
        }
        target.writeText(text)
        return RinLibrary(safeName, target, target.length(), target.lastModified())
    }

    // ---- رفع أرشيف مضغوط (ZIP) وفك ضغطه داخل المشروع، وتنزيل المشروع كأرشيف ----
    //
    // "رفع مضغوط": يقرأ ملف .zip من [uri] عبر ContentResolver ويفكّ ضغطه مباشرة داخل
    // مجلد المشروع، محافظاً على بنية المجلدات الداخلية للأرشيف (مثل lib/mylib.og.rin).
    // نتحقق من كل مسار داخل الأرشيف حتى لا يخرج ("Zip Slip") إلى خارج مجلد المشروع.

    /** يفكّ ضغط أرشيف ZIP من [uri] داخل مجلد المشروع، ويرجع عدد الملفات المستخرجة. */
    fun importZipFromUri(context: Context, project: Project, uri: Uri): Int {
        val resolver: ContentResolver = context.contentResolver
        val projectRoot = project.dir.canonicalFile
        var extractedCount = 0

        val input = resolver.openInputStream(uri)
            ?: throw IllegalStateException("تعذّرت قراءة الأرشيف المحدد")
        ZipInputStream(input).use { zip ->
            var entry: ZipEntry? = zip.nextEntry
            while (entry != null) {
                val safeRelPath = sanitizeZipEntryPath(entry.name)
                if (safeRelPath != null) {
                    val outFile = File(projectRoot, safeRelPath)
                    // تأكيد إضافي أن المسار الناتج ما زال داخل مجلد المشروع فعلياً.
                    if (outFile.canonicalFile.path.startsWith(projectRoot.path + File.separator)) {
                        if (entry.isDirectory) {
                            outFile.mkdirs()
                        } else {
                            outFile.parentFile?.mkdirs()
                            BufferedOutputStream(FileOutputStream(outFile)).use { out ->
                                zip.copyTo(out)
                            }
                            extractedCount++
                        }
                    }
                }
                zip.closeEntry()
                entry = zip.nextEntry
            }
        }
        return extractedCount
    }

    /** ينظّف مسار عنصر داخل الأرشيف ويرفض أي محاولة خروج خارج مجلد المشروع (../، مسار مطلق). */
    private fun sanitizeZipEntryPath(rawName: String): String? {
        val normalized = rawName.replace('\\', '/').trim('/')
        if (normalized.isEmpty()) return null
        val segments = normalized.split('/').filter { it.isNotEmpty() && it != "." }
        if (segments.any { it == ".." }) return null
        return segments.joinToString(File.separator)
    }

    /**
     * يضغط كل ملفات مجلد المشروع (بما فيها lib/) داخل أرشيف ZIP واحد في cacheDir، تمهيداً
     * لتنزيله عبر [RinDownloadManager]. يرجع الملف الناتج مع اسم عرض مناسب.
     */
    fun exportProjectAsZip(context: Context, project: Project): File {
        val cacheDir = File(context.cacheDir, "project_exports").apply { mkdirs() }
        val zipFile = File(cacheDir, "${project.name}.zip")
        if (zipFile.exists()) zipFile.delete()

        ZipOutputStream(BufferedOutputStream(FileOutputStream(zipFile))).use { zipOut ->
            val root = project.dir
            root.walkTopDown().filter { it.isFile }.forEach { file ->
                val relPath = file.relativeTo(root).path.replace(File.separatorChar, '/')
                zipOut.putNextEntry(ZipEntry(relPath))
                file.inputStream().use { it.copyTo(zipOut) }
                zipOut.closeEntry()
            }
        }
        return zipFile
    }

    private fun queryDisplayName(resolver: ContentResolver, uri: Uri): String? {
        return try {
            resolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { cursor ->
                if (cursor.moveToFirst()) {
                    val idx = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
                    if (idx >= 0) cursor.getString(idx) else null
                } else null
            }
        } catch (t: Throwable) {
            null
        }
    }
}
