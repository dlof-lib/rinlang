package com.dlof.rinlang

import java.io.File
import java.time.Instant

/**
 * البيانات الوصفية الكاملة لمشروع Rin، كما تُحفظ في ملف `project.og.urin` بجذر المشروع (المواصفة: docs/project-og-urin.md).
 *
 * الملف حاوية Rin مصغّرة (@container ... .end/container) من أسطر `key="value";`. الإصدار 2 يوسّع الإصدار 1
 * (الذي كان يحمل `type` فقط) بـ: id, name, version, description, author, license, tags, created, modified,
 * minRinVersion, permissions, dependencies, icon — مع الحفاظ على:
 *  - **التوافق الخلفي**: تُقرأ الصيغة الخام القديمة (`type=container`) وصيغة الحاوية v1؛ والتطبيقات القديمة التي
 *    تلتقط أول `type=` في الملف تجد النوع كما هو (نكتبه أولاً، ولا نستعمل أي حقل ينتهي بـ type).
 *  - **الحقول المجهولة**: تُحفظ في [extra] وتُعاد كتابتها كما هي، فلا تضيع حقول إصدار أحدث عند التعديل.
 *  - **الحدود**: أطوال الحقول وعدد عناصر القوائم محدودة (يطابق tools/rinpak.py: urin_parse).
 *
 * لا يعتمد هذا الملف على Android إلا بصنف File، ويطابق منطق `urin_parse/urin_render` في المرجع البايثوني.
 */
data class RinProjectMeta(
    val type: ProjectType = ProjectType.FREE,
    /** معرّف مستقر للمشروع ([a-z0-9._-])، يبقى ثابتاً عند إعادة التسمية والإصدارات. */
    val id: String = "",
    /** اسم العرض (يطابق اسم مجلد المشروع). */
    val name: String = "",
    val version: String = "",
    val description: String = "",
    val author: String = "",
    val license: String = "",
    val tags: List<String> = emptyList(),
    val created: String = "",
    val modified: String = "",
    val minRinVersion: String = "",
    val permissions: List<String> = emptyList(),
    val dependencies: List<String> = emptyList(),
    /** مسار نسبي لصورة PNG داخل المشروع تُستعمل أيقونةً للحزمة المصدَّرة (اختياري). */
    val icon: String = "",
    val extra: Map<String, String> = emptyMap(),
    /** إصدار صيغة الملف الذي قُرئ منه (1 = قديم). الكتابة دائماً بـ [SCHEMA]. */
    val schema: Int = 1
) {

    /** يقصّ الحقول إلى حدودها ويطهّر القوائم (يُستعمل قبل أي حفظ قادم من واجهة/حزمة). */
    fun normalized(): RinProjectMeta = copy(
        id = id.take(MAX_ID), name = name.take(MAX_NAME), version = version.take(MAX_VERSION),
        description = description.take(MAX_DESCRIPTION), author = author.take(MAX_AUTHOR),
        license = license.take(MAX_LICENSE), icon = icon.take(MAX_ICON_PATH), minRinVersion = minRinVersion.take(MAX_VERSION),
        tags = clean(tags, MAX_TAGS, MAX_TAG_LEN), permissions = clean(permissions, MAX_PERMS, MAX_PERM_LEN),
        dependencies = clean(dependencies, MAX_DEPS, MAX_DEP_LEN)
    )

    /**
     * يطبّق حقول مانيفست RinPak **المتحقَّق منه** فوق هذه البيانات. المانيفست هو المرجع (هو ما رآه المستخدم في
     * المعاينة وما يغطيه التوقيع)، فيتقدّم على أي قيمة مناقضة داخل project.og.urin المضمَّن في الحزمة، حتى لا
     * يمرّ تغيير غير معروض (كصلاحيات إضافية). الحقول التي لا يحملها المانيفست (icon, extra, created) تبقى.
     */
    fun withManifest(m: RinPak.Manifest): RinProjectMeta = copy(
        type = ProjectType.fromId(m.type),
        id = m.meta.id, version = m.meta.version, description = m.meta.description,
        author = m.meta.author, license = m.meta.license, tags = m.meta.tags,
        minRinVersion = m.meta.minRinVersion, permissions = m.meta.permissions, dependencies = m.meta.dependencies,
        created = created.ifBlank { m.created }
    ).normalized()

    companion object {
        const val FILE_NAME = "project.og.urin"
        /** إصدار الصيغة المكتوب حالياً. */
        const val SCHEMA = 2
        const val MAX_FILE_BYTES = 64 * 1024

        const val MAX_NAME = 64
        const val MAX_ID = 64
        const val MAX_VERSION = 32
        const val MAX_DESCRIPTION = 1000
        const val MAX_AUTHOR = 128
        const val MAX_LICENSE = 64
        const val MAX_ICON_PATH = 256
        const val MAX_TAGS = 16
        const val MAX_TAG_LEN = 32
        const val MAX_PERMS = 64
        const val MAX_PERM_LEN = 64
        const val MAX_DEPS = 64
        const val MAX_DEP_LEN = 128
        const val MAX_EXTRA = 32
        const val MAX_EXTRA_VALUE = 512

        /** سطر `key="value";` أو `key=value` (الصيغة الخام القديمة)، مع `;` اختيارية. */
        private val LINE = Regex("""\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(?:"((?:[^"\\]|\\.)*)"|([^;\s"]+))\s*;?\s*""")
        private val TYPE_ID = Regex("[A-Za-z0-9_]+")

        private fun clean(list: List<String>, maxItems: Int, maxLen: Int): List<String> =
            list.map { it.trim().take(maxLen) }.filter { it.isNotEmpty() }.take(maxItems)

        private fun splitList(v: String, maxItems: Int, maxLen: Int): List<String> =
            clean(v.split(','), maxItems, maxLen)

        fun escape(v: String): String = buildString {
            for (c in v) when (c) {
                '\\' -> append("\\\\")
                '"' -> append("\\\"")
                '\n' -> append("\\n")
                '\r' -> append("\\r")
                '\t' -> append("\\t")
                else -> append(c)
            }
        }

        fun unescape(v: String): String {
            val sb = StringBuilder(v.length)
            var i = 0
            while (i < v.length) {
                val c = v[i]
                if (c == '\\' && i + 1 < v.length) {
                    val n = v[i + 1]
                    sb.append(
                        when (n) {
                            'n' -> '\n'
                            'r' -> '\r'
                            't' -> '\t'
                            else -> n
                        }
                    )
                    i += 2
                } else {
                    sb.append(c)
                    i++
                }
            }
            return sb.toString()
        }

        /** يقرأ نص project.og.urin (أي إصدار). أي سطر غير مفهوم يُتجاهل؛ النوع غير الصالح يصبح FREE. */
        fun parse(text: String): RinProjectMeta {
            var m = RinProjectMeta()
            val extra = LinkedHashMap<String, String>()
            var typeId = ""
            for (line in text.lineSequence()) {
                val t = line.trim()
                if (t.isEmpty() || t.startsWith("#") || t.startsWith("//") || t.startsWith("@container") || t.startsWith(".end")) continue
                val match = LINE.matchEntire(line) ?: continue
                val key = match.groupValues[1]
                val quoted = match.groups[2]
                val value = if (quoted != null) unescape(quoted.value) else match.groupValues[3]
                when (key) {
                    "schema" -> m = m.copy(schema = value.toIntOrNull() ?: 1)
                    "type" -> typeId = value.take(64)
                    "id" -> m = m.copy(id = value)
                    "name" -> m = m.copy(name = value)
                    "version" -> m = m.copy(version = value)
                    "description" -> m = m.copy(description = value)
                    "author" -> m = m.copy(author = value)
                    "license" -> m = m.copy(license = value)
                    "created" -> m = m.copy(created = value)
                    "modified" -> m = m.copy(modified = value)
                    "minRinVersion" -> m = m.copy(minRinVersion = value)
                    "icon" -> m = m.copy(icon = value)
                    "tags" -> m = m.copy(tags = splitList(value, MAX_TAGS, MAX_TAG_LEN))
                    "permissions" -> m = m.copy(permissions = splitList(value, MAX_PERMS, MAX_PERM_LEN))
                    "dependencies" -> m = m.copy(dependencies = splitList(value, MAX_DEPS, MAX_DEP_LEN))
                    else -> if (extra.size < MAX_EXTRA) extra[key] = value.take(MAX_EXTRA_VALUE)
                }
            }
            val type = if (TYPE_ID.matches(typeId)) ProjectType.fromId(typeId) else ProjectType.FREE
            return m.copy(type = type, extra = extra).normalized()
        }

        /** يولّد نص project.og.urin بالإصدار [SCHEMA]. النوع أولاً دائماً (توافق التطبيقات القديمة). */
        fun render(m: RinProjectMeta): String = buildString {
            append("# بيانات وصفية لمشروع Rin — يُدار تلقائياً من التطبيق، يمكن تعديله بحذر\n")
            append("// الحقول: النوع، المعرّف، الإصدار، المؤلف، الرخصة، الوسوم، الصلاحيات...\n")
            append("@container=ProjectMeta\n")
            append("    schema=").append(SCHEMA).append(";\n")
            append("    type=\"").append(m.type.id).append("\";\n")
            fun put(key: String, value: String) {
                if (value.isNotEmpty()) append("    ").append(key).append("=\"").append(escape(value)).append("\";\n")
            }
            put("id", m.id)
            put("name", m.name)
            put("version", m.version)
            put("description", m.description)
            put("author", m.author)
            put("license", m.license)
            put("tags", m.tags.joinToString(","))
            put("created", m.created)
            put("modified", m.modified)
            put("minRinVersion", m.minRinVersion)
            put("permissions", m.permissions.joinToString(","))
            put("dependencies", m.dependencies.joinToString(","))
            put("icon", m.icon)
            for ((k, v) in m.extra.toSortedMap()) put(k, v)
            append(".end/container\n")
        }

        /** يقرأ project.og.urin من [dir]، أو null إن غاب/كبر/تلف. */
        fun read(dir: File): RinProjectMeta? {
            val f = File(dir, FILE_NAME)
            if (!f.isFile || f.length() > MAX_FILE_BYTES) return null
            return try {
                parse(f.readText(Charsets.UTF_8))
            } catch (_: Exception) {
                null
            }
        }

        /** كتابة شبه ذرية (ملف مخفي مؤقت ثم إعادة تسمية) لتفادي ملف نصف مكتوب عند انقطاع التطبيق. */
        fun write(dir: File, meta: RinProjectMeta) {
            val target = File(dir, FILE_NAME)
            val text = render(meta.normalized())
            val tmp = File(dir, ".$FILE_NAME.tmp")
            try {
                tmp.writeText(text, Charsets.UTF_8)
                if (!tmp.renameTo(target)) target.writeText(text, Charsets.UTF_8)
            } finally {
                if (tmp.exists()) tmp.delete()
            }
        }

        /** بيانات مشروع جديد. */
        fun forNewProject(type: ProjectType, name: String): RinProjectMeta {
            val now = Instant.now().toString()
            return RinProjectMeta(
                type = type, id = RinPak.makeId(name), name = name, version = "1.0.0", created = now, modified = now, schema = SCHEMA
            ).normalized()
        }

        /**
         * يضمن وجود project.og.urin بالنوع/الاسم الصحيحين دون فقدان بقية الحقول. [manifest] (إن وُجد) يتقدّم على
         * القيم المضمَّنة في الملف ([withManifest]). يعيد القيمة المكتوبة.
         */
        fun ensure(dir: File, type: ProjectType, name: String, manifest: RinPak.Manifest? = null): RinProjectMeta {
            val now = Instant.now().toString()
            var m = read(dir) ?: RinProjectMeta()
            if (manifest != null) m = m.withManifest(manifest)
            m = m.copy(type = type, name = name, modified = now, schema = SCHEMA)
            if (m.id.isBlank()) m = m.copy(id = RinPak.makeId(name))
            if (m.version.isBlank()) m = m.copy(version = "1.0.0")
            if (m.created.isBlank()) m = m.copy(created = now)
            m = m.normalized()
            write(dir, m)
            return m
        }

        /** يعدّل البيانات الحالية (أو الافتراضية) ويحفظها، ويحدّث `modified`. */
        fun update(dir: File, type: ProjectType, name: String, change: (RinProjectMeta) -> RinProjectMeta): RinProjectMeta {
            val base = read(dir) ?: forNewProject(type, name)
            val next = change(base).copy(modified = Instant.now().toString(), schema = SCHEMA).normalized()
            write(dir, next)
            return next
        }

        private val PNG_MAGIC = byteArrayOf(0x89.toByte(), 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A)

        /** ملف أيقونة المشروع إن كان [rel] مساراً آمناً داخل المشروع لصورة PNG ≤ 512KB، وإلا null. */
        fun resolveIcon(dir: File, rel: String): File? {
            if (rel.isEmpty() || RinPak.safePath(rel) == null || rel.split('/').any { it.startsWith(".") }) return null
            return try {
                val root = dir.canonicalFile
                val f = File(root, rel).canonicalFile
                if (!f.path.startsWith(root.path + File.separator)) return null
                if (!f.isFile || f.length() > RinPak.MAX_ICON_BYTES || f.length() < PNG_MAGIC.size) return null
                val head = ByteArray(PNG_MAGIC.size)
                f.inputStream().use { if (it.read(head) != head.size) return null }
                if (head.contentEquals(PNG_MAGIC)) f else null
            } catch (_: Exception) {
                null
            }
        }
    }
}
