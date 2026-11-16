package com.dlof.rinlang.apk.dex

import java.io.ByteArrayOutputStream
import java.security.MessageDigest
import java.util.zip.Adler32

/**
 * كاتب ملفات DEX حقيقي (صيغة 035) بلا أي اعتماد على أندرويد أو مكتبات خارجية — يُشغَّل على
 * الجهاز نفسه داخل RinStudio أثناء "تصدير APK" لإنتاج classes.dex صالح يقبله مُحمِّل ART/dexopt.
 *
 * ما يكتبه فعلاً (بحسب مواصفة dex-format من AOSP):
 *  - header (0x70 بايت) مع checksum = Adler-32 وsignature = SHA-1 محسوبَين فعلياً.
 *  - string_ids / type_ids / proto_ids / field_ids / method_ids / class_defs مرتّبة بالترتيب
 *    الإلزامي (النصوص بترتيب وحدات UTF-16، الأنواع بحسب نصّ الوصف، البروتوتايبات بنوع الإرجاع ثم
 *    المعاملات، الحقول والدوال بحسب (الصنف، الاسم، النوع/البروتوتايب)).
 *  - data: type_list + code_item + string_data_item (MUTF-8) + class_data_item + map_list.
 *
 * الواجهة رمزية: تُنشئ [StrRef]/[TypeRef]/[ProtoRef]/[FieldRef]/[MethodRef] أثناء توليد الشيفرة،
 * ولا تُحسَم أرقام الفهارس إلا في [build] بعد الفرز النهائي — فتُشفَّر التعليمات (انظر [CodeBuilder])
 * بالأرقام الصحيحة وقت البناء فقط.
 */
class DexBuilder {

    // ---------------------------------------------------------------- مراجع رمزية
    class StrRef internal constructor(val value: String) { internal var idx = -1 }
    class TypeRef internal constructor(val desc: String, internal val str: StrRef) { internal var idx = -1 }
    class ProtoRef internal constructor(
        val ret: TypeRef, val params: List<TypeRef>, internal val shorty: StrRef
    ) { internal var idx = -1; internal var typeListOff = 0 }
    class FieldRef internal constructor(val owner: TypeRef, val type: TypeRef, val name: StrRef) { internal var idx = -1 }
    class MethodRef internal constructor(val owner: TypeRef, val proto: ProtoRef, val name: StrRef) { internal var idx = -1 }

    private val strings = LinkedHashMap<String, StrRef>()
    private val types = LinkedHashMap<String, TypeRef>()
    private val protos = LinkedHashMap<String, ProtoRef>()
    private val fields = LinkedHashMap<String, FieldRef>()
    private val methods = LinkedHashMap<String, MethodRef>()
    private val classes = ArrayList<ClassDef>()

    fun str(s: String): StrRef = strings.getOrPut(s) { StrRef(s) }

    fun type(desc: String): TypeRef = types.getOrPut(desc) { TypeRef(desc, str(desc)) }

    fun proto(ret: String, params: List<String>): ProtoRef {
        val key = ret + "(" + params.joinToString("") + ")"
        return protos.getOrPut(key) {
            val sb = StringBuilder()
            sb.append(shortyChar(ret))
            params.forEach { sb.append(shortyChar(it)) }
            ProtoRef(type(ret), params.map { type(it) }, str(sb.toString()))
        }
    }

    fun field(owner: String, name: String, type: String): FieldRef =
        fields.getOrPut("$owner->$name:$type") { FieldRef(type(owner), type(type), str(name)) }

    fun method(owner: String, name: String, ret: String, params: List<String>): MethodRef =
        methods.getOrPut("$owner->$name(${params.joinToString("")})$ret") {
            MethodRef(type(owner), proto(ret, params), str(name))
        }

    private fun shortyChar(desc: String): Char = when (desc[0]) {
        'L', '[' -> 'L'
        else -> desc[0]
    }

    // ---------------------------------------------------------------- تعريف الأصناف
    class FieldDef(val ref: FieldRef, val access: Int)
    class MethodDef(val ref: MethodRef, val access: Int, val code: CodeBuilder?)
    class ClassDef(
        val type: TypeRef, val access: Int, val superType: TypeRef,
        val sourceFile: StrRef?,
        val staticFields: List<FieldDef>, val directMethods: List<MethodDef>, val virtualMethods: List<MethodDef>
    )

    fun addClass(
        desc: String, access: Int, superDesc: String = "Ljava/lang/Object;", sourceFile: String? = null,
        staticFields: List<FieldDef> = emptyList(),
        directMethods: List<MethodDef> = emptyList(), virtualMethods: List<MethodDef> = emptyList()
    ) {
        classes.add(ClassDef(type(desc), access, type(superDesc), sourceFile?.let { str(it) },
            staticFields, directMethods, virtualMethods))
    }

    fun newCode(): CodeBuilder = CodeBuilder(this)

    // ---------------------------------------------------------------- البناء
    fun build(): ByteArray {
        require(classes.isNotEmpty()) { "dex بلا أصناف" }

        // الدوال المُعرَّفة داخل الأصناف يجب أن تكون ضمن method_ids حتى لو لم تُستدعَ (مُسجَّلة سلفاً عبر method()).
        // 1) النصوص: كل النصوص المسجَّلة + نصوص const-string داخل الشيفرة (تُسجَّل أثناء التوليد عبر str()).
        val sortedStrings = strings.values.sortedWith { a, b -> a.value.compareTo(b.value) }
        sortedStrings.forEachIndexed { i, s -> s.idx = i }

        // 2) الأنواع مرتّبة بترتيب نصّ الوصف == ترتيب فهرس النص (لأن النصوص مرتّبة)
        val sortedTypes = types.values.sortedBy { it.str.idx }
        sortedTypes.forEachIndexed { i, t -> t.idx = i }

        // 3) البروتوتايب: (نوع الإرجاع، ثم قائمة المعاملات معجمياً)
        val sortedProtos = protos.values.sortedWith { a, b ->
            var c = a.ret.idx.compareTo(b.ret.idx)
            if (c == 0) {
                val n = minOf(a.params.size, b.params.size)
                for (i in 0 until n) { c = a.params[i].idx.compareTo(b.params[i].idx); if (c != 0) break }
                if (c == 0) c = a.params.size.compareTo(b.params.size)
            }
            c
        }
        sortedProtos.forEachIndexed { i, p -> p.idx = i }

        val sortedFields = fields.values.sortedWith(
            compareBy<FieldRef>({ it.owner.idx }, { it.name.idx }, { it.type.idx }))
        sortedFields.forEachIndexed { i, f -> f.idx = i }

        val sortedMethods = methods.values.sortedWith(
            compareBy<MethodRef>({ it.owner.idx }, { it.name.idx }, { it.proto.idx }))
        sortedMethods.forEachIndexed { i, m -> m.idx = i }

        // ---- تخطيط الأقسام ----
        val headerSize = 0x70
        val stringIdsOff = headerSize
        val typeIdsOff = stringIdsOff + sortedStrings.size * 4
        val protoIdsOff = typeIdsOff + sortedTypes.size * 4
        val fieldIdsOff = protoIdsOff + sortedProtos.size * 12
        val methodIdsOff = fieldIdsOff + sortedFields.size * 8
        val classDefsOff = methodIdsOff + sortedMethods.size * 8
        val dataOff = classDefsOff + classes.size * 32

        val data = Buf()
        val mapEntries = ArrayList<IntArray>() // type, size, offset(absolute)

        // type_lists (محاذاة 4)
        val typeListStart = data.size()
        var typeListCount = 0
        for (p in sortedProtos) {
            if (p.params.isEmpty()) { p.typeListOff = 0; continue }
            data.align(4)
            p.typeListOff = dataOff + data.size()
            data.u4(p.params.size)
            p.params.forEach { data.u2(it.idx) }
            typeListCount++
        }
        if (typeListCount > 0) mapEntries.add(intArrayOf(0x1001, typeListCount, dataOff + typeListStart))

        // code_items
        data.align(4)
        val codeStart = data.size()
        var codeCount = 0
        val codeOffs = HashMap<MethodDef, Int>()
        for (cls in classes) {
            for (m in cls.directMethods + cls.virtualMethods) {
                val code = m.code ?: continue
                data.align(4)
                codeOffs[m] = dataOff + data.size()
                code.writeCodeItem(data)
                codeCount++
            }
        }
        if (codeCount > 0) mapEntries.add(intArrayOf(0x2001, codeCount, dataOff + codeStart))

        // string_data_items
        val stringDataOff = IntArray(sortedStrings.size)
        val strDataStart = data.size()
        sortedStrings.forEachIndexed { i, s ->
            stringDataOff[i] = dataOff + data.size()
            data.uleb(s.value.length) // طول بوحدات UTF-16
            data.bytes(mutf8(s.value))
            data.u1(0)
        }
        if (sortedStrings.isNotEmpty()) mapEntries.add(intArrayOf(0x2002, sortedStrings.size, dataOff + strDataStart))

        // class_data_items
        val classDataOff = IntArray(classes.size)
        val cdStart = data.size()
        classes.forEachIndexed { ci, cls ->
            classDataOff[ci] = dataOff + data.size()
            val sf = cls.staticFields.sortedBy { it.ref.idx }
            val dm = cls.directMethods.sortedBy { it.ref.idx }
            val vm = cls.virtualMethods.sortedBy { it.ref.idx }
            data.uleb(sf.size); data.uleb(0); data.uleb(dm.size); data.uleb(vm.size)
            var prev = 0
            for (f in sf) { data.uleb(f.ref.idx - prev); prev = f.ref.idx; data.uleb(f.access) }
            prev = 0
            for (m in dm) { data.uleb(m.ref.idx - prev); prev = m.ref.idx; data.uleb(m.access); data.uleb(codeOffs[m] ?: 0) }
            prev = 0
            for (m in vm) { data.uleb(m.ref.idx - prev); prev = m.ref.idx; data.uleb(m.access); data.uleb(codeOffs[m] ?: 0) }
        }
        mapEntries.add(intArrayOf(0x2000, classes.size, dataOff + cdStart))

        // map_list
        data.align(4)
        val mapOff = dataOff + data.size()

        val allMap = ArrayList<IntArray>()
        allMap.add(intArrayOf(0x0000, 1, 0))
        if (sortedStrings.isNotEmpty()) allMap.add(intArrayOf(0x0001, sortedStrings.size, stringIdsOff))
        if (sortedTypes.isNotEmpty()) allMap.add(intArrayOf(0x0002, sortedTypes.size, typeIdsOff))
        if (sortedProtos.isNotEmpty()) allMap.add(intArrayOf(0x0003, sortedProtos.size, protoIdsOff))
        if (sortedFields.isNotEmpty()) allMap.add(intArrayOf(0x0004, sortedFields.size, fieldIdsOff))
        if (sortedMethods.isNotEmpty()) allMap.add(intArrayOf(0x0005, sortedMethods.size, methodIdsOff))
        allMap.add(intArrayOf(0x0006, classes.size, classDefsOff))
        allMap.addAll(mapEntries)
        allMap.add(intArrayOf(0x1000, 1, mapOff))
        allMap.sortBy { it[2] }

        data.u4(allMap.size)
        for (e in allMap) { data.u2(e[0]); data.u2(0); data.u4(e[1]); data.u4(e[2]) }

        val fileSize = dataOff + data.size()
        val dataSize = data.size()

        // ---- كتابة الملف الكامل ----
        val out = Buf()
        // header (checksum/signature تُملأ لاحقاً)
        out.bytes(byteArrayOf('d'.code.toByte(), 'e'.code.toByte(), 'x'.code.toByte(), 0x0a, '0'.code.toByte(), '3'.code.toByte(), '5'.code.toByte(), 0))
        out.u4(0)                       // checksum
        out.bytes(ByteArray(20))        // signature
        out.u4(fileSize)
        out.u4(headerSize)
        out.u4(0x12345678)              // endian_tag
        out.u4(0); out.u4(0)            // link_size / link_off
        out.u4(mapOff)
        out.u4(sortedStrings.size); out.u4(if (sortedStrings.isEmpty()) 0 else stringIdsOff)
        out.u4(sortedTypes.size); out.u4(if (sortedTypes.isEmpty()) 0 else typeIdsOff)
        out.u4(sortedProtos.size); out.u4(if (sortedProtos.isEmpty()) 0 else protoIdsOff)
        out.u4(sortedFields.size); out.u4(if (sortedFields.isEmpty()) 0 else fieldIdsOff)
        out.u4(sortedMethods.size); out.u4(if (sortedMethods.isEmpty()) 0 else methodIdsOff)
        out.u4(classes.size); out.u4(classDefsOff)
        out.u4(dataSize); out.u4(dataOff)
        check(out.size() == headerSize) { "header size ${out.size()}" }

        sortedStrings.forEachIndexed { i, _ -> out.u4(stringDataOff[i]) }
        sortedTypes.forEach { out.u4(it.str.idx) }
        for (p in sortedProtos) { out.u4(p.shorty.idx); out.u4(p.ret.idx); out.u4(p.typeListOff) }
        for (f in sortedFields) { out.u2(f.owner.idx); out.u2(f.type.idx); out.u4(f.name.idx) }
        for (m in sortedMethods) { out.u2(m.owner.idx); out.u2(m.proto.idx); out.u4(m.name.idx) }
        classes.forEachIndexed { ci, cls ->
            out.u4(cls.type.idx); out.u4(cls.access); out.u4(cls.superType.idx)
            out.u4(0)                                   // interfaces_off
            out.u4(cls.sourceFile?.idx ?: -1)           // NO_INDEX
            out.u4(0)                                   // annotations_off
            out.u4(classDataOff[ci])
            out.u4(0)                                   // static_values_off
        }
        check(out.size() == dataOff) { "data offset mismatch ${out.size()} != $dataOff" }
        out.bytes(data.toByteArray())

        val bytes = out.toByteArray()
        check(bytes.size == fileSize) { "file size mismatch ${bytes.size} != $fileSize" }

        // signature = SHA-1(bytes[32..]) ثم checksum = Adler-32(bytes[12..])
        val sha = MessageDigest.getInstance("SHA-1")
        sha.update(bytes, 32, bytes.size - 32)
        System.arraycopy(sha.digest(), 0, bytes, 12, 20)
        val adler = Adler32()
        adler.update(bytes, 12, bytes.size - 12)
        val ck = adler.value.toInt()
        bytes[8] = ck.toByte(); bytes[9] = (ck ushr 8).toByte()
        bytes[10] = (ck ushr 16).toByte(); bytes[11] = (ck ushr 24).toByte()
        return bytes
    }

    // ---------------------------------------------------------------- أدوات الترميز
    internal class Buf {
        private val bo = ByteArrayOutputStream()
        fun size() = bo.size()
        fun u1(v: Int) { bo.write(v and 0xff) }
        fun u2(v: Int) { bo.write(v and 0xff); bo.write((v ushr 8) and 0xff) }
        fun u4(v: Int) { u2(v and 0xffff); u2((v ushr 16) and 0xffff) }
        fun bytes(b: ByteArray) { bo.write(b, 0, b.size) }
        fun align(n: Int) { while (bo.size() % n != 0) bo.write(0) }
        fun uleb(v: Int) {
            var x = v
            while (true) {
                val b = x and 0x7f
                x = x ushr 7
                if (x == 0) { bo.write(b); return }
                bo.write(b or 0x80)
            }
        }
        fun toByteArray(): ByteArray = bo.toByteArray()
    }

    companion object {
        /** ترميز Modified UTF-8 كما تتطلبه DEX: U+0000 → C0 80، والأزواج البديلة تُرمَّز كلٌّ على حدة (3 بايت). */
        fun mutf8(s: String): ByteArray {
            val bo = ByteArrayOutputStream(s.length + 8)
            for (ch in s) {
                val c = ch.code
                when {
                    c in 1..0x7f -> bo.write(c)
                    c < 0x800 -> { bo.write(0xc0 or (c shr 6)); bo.write(0x80 or (c and 0x3f)) }
                    else -> {
                        bo.write(0xe0 or (c shr 12)); bo.write(0x80 or ((c shr 6) and 0x3f)); bo.write(0x80 or (c and 0x3f))
                    }
                }
            }
            return bo.toByteArray()
        }

        const val ACC_PUBLIC = 0x0001
        const val ACC_STATIC = 0x0008
        const val ACC_FINAL = 0x0010
        const val ACC_CONSTRUCTOR = 0x10000
    }
}
