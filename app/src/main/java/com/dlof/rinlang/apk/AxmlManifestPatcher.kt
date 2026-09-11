package com.dlof.rinlang.apk

import java.io.ByteArrayOutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * محرِّر ثنائي حقيقي لـ AndroidManifest.xml المُصرَّف (تنسيق AXML/ARSC القياسي لأندرويد) —
 * بلا الاعتماد على aapt/aapt2 (غير متاحَين على الجهاز وقت التصدير).
 *
 * الفكرة: بيان الحزمة المضيفة (RinStudio نفسها) مُصرَّف بالفعل وصحيح بنيوياً. بدل توليد
 * AXML من الصفر (عمل ضخم ومعرّض للأخطاء)، نأخذ نفس البيان الثنائي ونُبدّل فيه قيمتين فقط
 * على مستوى البايت:
 *   1) قيمة attribute باسم "package" في عنصر الجذر <manifest> → معرّف تطبيق فريد جديد.
 *   2) قيمة attribute باسم "label" في عنصر <application> → اسم المشروع المعروض، كنص حرفي
 *      جديد بدل مرجع "@string/app_name" (فنتجنّب لمس resources.arsc كلياً).
 *
 * كلا التغييرين يتطلبان إضافة سلسلتين نصيتين جديدتين فقط لمجمّع السلاسل (String Pool) في
 * بداية الملف؛ باقي الشجرة (العناصر، السمات الأخرى، خريطة الموارد) تبقى كما هي بايتاً بايت،
 * لأن كل الإشارات داخل الشجرة هي *فهارس* ضمن مجمّع السلاسل، وليست إزاحات بايت مطلقة —
 * إضافة سلسلتين في آخر المجمّع لا يُغيّر فهرس أي سلسلة موجودة سابقاً.
 *
 * مرجع التنسيق: ResChunk_header / ResStringPool_header / ResXMLTree_node القياسية
 * (نفس التنسيق الذي تعتمده أدوات مثل apktool/AXMLPrinter منذ سنوات، وهو ثابت عبر إصدارات
 * أندرويد لأنه جزء من ABI الجهاز نفسه).
 */
object AxmlManifestPatcher {

    private const val CHUNK_XML_TOP = 0x0003
    private const val CHUNK_STRING_POOL = 0x0001
    private const val CHUNK_RESOURCE_MAP = 0x0180
    private const val CHUNK_XML_START_ELEMENT = 0x0102

    private const val FLAG_UTF8 = 0x00000100

    private const val TYPE_STRING = 0x03
    private const val TYPE_INT_DEC = 0x10
    private const val NO_ENTRY = -1 // 0xFFFFFFFF كـ Int موقَّع — يعني "بلا سلسلة خام، استخدم القيمة المُطبَّعة"

    class ManifestFormatException(message: String) : Exception(message)

    data class Result(val bytes: ByteArray, val appliedCount: Int, val intAppliedCount: Int = 0)

    /**
     * سمة واحدة مطلوب تعديل قيمتها: العنصر الذي تنتمي إليه (اسم tag، مثل "manifest" أو
     * "application" أو "provider") واسم السمة (بلا namespace، مثل "package"/"label"/
     * "authorities") والقيمة النصية الجديدة. مطلوبة، وإلا تُرمى [ManifestFormatException].
     */
    data class AttrPatch(val elementName: String, val attrName: String, val newValue: String)

    /**
     * سمة عددية صحيحة (مثل android:minSdkVersion/targetSdkVersion داخل &lt;uses-sdk&gt;) —
     * تُكتَب كقيمة TYPE_INT_DEC مباشرة في الشجرة بلا أي إضافة لمجمّع السلاسل (لا حاجة لها
     * أصلاً لقيمة عددية)، خلافاً لـ [AttrPatch] النصية.
     */
    data class IntAttrPatch(val elementName: String, val attrName: String, val newValue: Int)

    /**
     * يُرجع نسخة معدَّلة من [original] (بايتات AndroidManifest.xml المصرَّفة كما استُخرجت من
     * الحزمة المضيفة) بعد تطبيق كل [patches] دفعة واحدة (فتصفّح واحد لشجرة العقد، وإضافة
     * سلسلة جديدة واحدة لكل تعديل في نهاية مجمّع السلاسل).
     *
     * [authorityRewrite]، إن مُرِّر، يُطبَّق على *كل* سمة "authorities" في *كل* عنصر (لا يقتصر
     * على أول تطابق كـ [AttrPatch])، ويُستدعى بالقيمة الحالية ليُرجِع القيمة الجديدة. ضروري
     * لأن مكتبات AndroidX/Firebase (WorkManager، Firebase Auth، ...) تُضيف عناصر <provider>
     * إضافية بسلطات (authorities) مبنية هي الأخرى من applicationId وقت بناء
     * RinStudio نفسها ومحفوظة كنص حرفي في البيان المُصرَّف — تركها بلا تفريد يسبّب تعارض سلطة
     * موفِّر (provider authority) مع RinStudio نفسها إن كانت مثبَّتة على الجهاز، فيفشل تثبيت
     * الحزمة المُصدَّرة تماماً (أندرويد يمنع سلطتين متطابقتين من حزمتين مختلفتين).
     *
     * إن لم يجد سمة متوقعة (بنية غير معتادة) يرمي [ManifestFormatException] بدل إفساد
     * البيان بصمت — يلتقطها RinApkExporter ويوقف التصدير مع رسالة واضحة.
     */
    fun patch(original: ByteArray, patches: List<AttrPatch>, authorityRewrite: ((String) -> String)? = null, intPatches: List<IntAttrPatch> = emptyList()): Result {
        val buf = ByteBuffer.wrap(original.copyOf()).order(ByteOrder.LITTLE_ENDIAN)
        val bytes = buf.array()

        if (bytes.size < 8 || (buf.getShort(0).toInt() and 0xFFFF) != CHUNK_XML_TOP) {
            throw ManifestFormatException("ليس ملف AndroidManifest.xml ثنائي صالح (رأس الملف غير متطابق)")
        }
        val totalSize = buf.getInt(4)

        // --- 1) قراءة مجمّع السلاسل ---
        val spOffset = 8
        if ((buf.getShort(spOffset).toInt() and 0xFFFF) != CHUNK_STRING_POOL) {
            throw ManifestFormatException("القطعة الثانية ليست String Pool كما هو متوقع")
        }
        val spHeaderSize = buf.getShort(spOffset + 2).toInt() and 0xFFFF
        val spChunkSize = buf.getInt(spOffset + 4)
        val stringCount = buf.getInt(spOffset + 8)
        val styleCount = buf.getInt(spOffset + 12)
        val flags = buf.getInt(spOffset + 16)
        val stringsStart = buf.getInt(spOffset + 20)
        val isUtf8 = (flags and FLAG_UTF8) != 0

        if (styleCount != 0) {
            // لا وجود لنصوص منسّقة (spans) داخل بيان حقيقي عملياً؛ نتجنّب التعقيد الإضافي بدل
            // المخاطرة بكسر البنية على حالة نادرة لم نختبرها.
            throw ManifestFormatException("مجمّع سلاسل يحوي style spans غير مدعوم في هذا المسار")
        }

        val offsetsBase = spOffset + spHeaderSize
        val strings = ArrayList<String>(stringCount)
        for (i in 0 until stringCount) {
            val rel = buf.getInt(offsetsBase + i * 4)
            val abs = spOffset + stringsStart + rel
            strings.add(if (isUtf8) readUtf8(buf, abs) else readUtf16(buf, abs))
        }

        // فهرس كل عنصر/سمة نحتاجها، مطلوبة كلها موجودة سلفاً في مجمّع السلاسل (أي بيان أندرويد
        // عادي يحوي "manifest" اسم عنصر جذر، وأسماء السمات كنصوص عادية بصرف النظر عن قيمها).
        fun requiredIdx(name: String): Int {
            val i = strings.indexOf(name)
            if (i < 0) throw ManifestFormatException("لم يُعثر على \"$name\" داخل مجمّع سلاسل البيان")
            return i
        }
        data class Resolved(val elementIdx: Int, val attrIdx: Int, val newStringIdx: Int, val encoded: ByteArray)

        val resolved = patches.mapIndexed { i, p ->
            Resolved(
                elementIdx = requiredIdx(p.elementName),
                attrIdx = requiredIdx(p.attrName),
                newStringIdx = stringCount + i,
                encoded = if (isUtf8) encodeUtf8(p.newValue) else encodeUtf16(p.newValue)
            )
        }

        // سمات عددية (minSdkVersion/targetSdkVersion وغيرها) — بحث "متساهل" هنا (لا يرمي عند
        // غيابها، فقط يتجاهلها) لأن هذه قدرة إضافية اختيارية قد لا تنطبق على كل بنية بيان
        // محتملة، خلافاً لسمات package/label/authorities الأساسية التي يجب أن تكون موجودة دوماً.
        fun optionalIdx(name: String): Int = strings.indexOf(name)
        data class ResolvedInt(val elementIdx: Int, val attrIdx: Int, val newValue: Int)
        val resolvedInt = intPatches.mapNotNull { p ->
            val elIdx = optionalIdx(p.elementName)
            val atIdx = optionalIdx(p.attrName)
            if (elIdx < 0 || atIdx < 0) null else ResolvedInt(elIdx, atIdx, p.newValue)
        }
        val appliedInt = BooleanArray(resolvedInt.size)

        val idxAuthorities = if (authorityRewrite != null) strings.indexOf("authorities") else -1
        // مواقع كل سمة authorities عُثر عليها أثناء التصفّح، بانتظار تخصيص فهرس سلسلة جديد
        // لكل واحدة بعد معرفة عددها (لا يُعرف مسبقاً كم عنصر <provider> موجود).
        data class AuthorityHit(val attrOffset: Int, val currentValue: String)
        val authorityHits = ArrayList<AuthorityHit>()

        // --- 2) تصفّح عقد XML بعد مجمّع السلاسل (وخريطة الموارد الاختيارية) لتطبيق كل تعديل ---
        var pos = spOffset + spChunkSize
        if (pos + 8 <= bytes.size && (buf.getShort(pos).toInt() and 0xFFFF) == CHUNK_RESOURCE_MAP) {
            pos += buf.getInt(pos + 4)
        }

        val applied = BooleanArray(resolved.size)

        while (pos + 8 <= bytes.size) {
            val chunkType = buf.getShort(pos).toInt() and 0xFFFF
            val chunkHeaderSize = buf.getShort(pos + 2).toInt() and 0xFFFF
            val chunkSize = buf.getInt(pos + 4)
            if (chunkSize <= 0 || pos + chunkSize > bytes.size) break

            if (chunkType == CHUNK_XML_START_ELEMENT) {
                // بعد رأس العقدة القياسي (16 بايت: type/headerSize/size/lineNumber/comment)
                // يأتي رأس العنصر الخاص بـ START_ELEMENT:
                //   nsUri(4) name(4) attrStart(2) attrSize(2) attrCount(2) idIdx(2) classIdx(2) styleIdx(2)
                val elemHeaderBase = pos + chunkHeaderSize
                val nameIdx = buf.getInt(elemHeaderBase + 4)
                val attrStart = buf.getShort(elemHeaderBase + 8).toInt() and 0xFFFF
                val attrSizeEach = buf.getShort(elemHeaderBase + 10).toInt() and 0xFFFF
                val attrCount = buf.getShort(elemHeaderBase + 12).toInt() and 0xFFFF
                val attrsBase = elemHeaderBase + attrStart

                for (a in 0 until attrCount) {
                    val off = attrsBase + a * attrSizeEach
                    if (off + 20 > bytes.size) continue
                    val attrNameIdx = buf.getInt(off + 4)

                    for (ri in resolved.indices) {
                        if (applied[ri]) continue
                        val r = resolved[ri]
                        if (nameIdx == r.elementIdx && attrNameIdx == r.attrIdx) {
                            buf.putInt(off + 8, r.newStringIdx)        // rawValueIdx
                            buf.put(off + 15, TYPE_STRING.toByte())    // دائماً كسلسلة حرفية (كانت قد تكون TYPE_REFERENCE)
                            buf.putInt(off + 16, r.newStringIdx)       // typedValue.data
                            applied[ri] = true
                        }
                    }
                    for (ri in resolvedInt.indices) {
                        if (appliedInt[ri]) continue
                        val r = resolvedInt[ri]
                        if (nameIdx == r.elementIdx && attrNameIdx == r.attrIdx) {
                            buf.putInt(off + 8, NO_ENTRY)              // rawValueIdx: بلا سلسلة، القيمة عددية خالصة
                            buf.put(off + 15, TYPE_INT_DEC.toByte())   // typedValue.dataType
                            buf.putInt(off + 16, r.newValue)           // typedValue.data = القيمة العددية مباشرة
                            appliedInt[ri] = true
                        }
                    }
                    if (idxAuthorities >= 0 && attrNameIdx == idxAuthorities) {
                        val currentRawIdx = buf.getInt(off + 8)
                        val currentValue = if (currentRawIdx in strings.indices) strings[currentRawIdx] else ""
                        authorityHits.add(AuthorityHit(off, currentValue))
                    }
                }
            }
            pos += chunkSize
        }

        for (ri in resolved.indices) {
            if (!applied[ri]) {
                val p = patches[ri]
                throw ManifestFormatException("لم يُعثر على سمة \"${p.attrName}\" داخل <${p.elementName}>")
            }
        }

        // سلاسل authorities الجديدة (فهرس واحد لكل مُدخل بعد سلاسل [resolved] الثابتة)
        val authorityEncoded = authorityHits.map { hit ->
            val newValue = authorityRewrite!!(hit.currentValue)
            if (isUtf8) encodeUtf8(newValue) else encodeUtf16(newValue)
        }
        val authorityBaseIdx = stringCount + resolved.size
        for (i in authorityHits.indices) {
            val off = authorityHits[i].attrOffset
            val newIdx = authorityBaseIdx + i
            buf.putInt(off + 8, newIdx)
            buf.put(off + 15, TYPE_STRING.toByte())
            buf.putInt(off + 16, newIdx)
        }

        // --- 3) إعادة بناء مجمّع السلاسل بإضافة سلسلة جديدة واحدة لكل تعديل في آخره ---
        val oldBlob = bytes.copyOfRange(spOffset + stringsStart, spOffset + spChunkSize)
        var blob = oldBlob
        val newOffsets = IntArray(resolved.size + authorityEncoded.size)
        for (i in resolved.indices) {
            newOffsets[i] = blob.size
            blob = blob + resolved[i].encoded
        }
        for (i in authorityEncoded.indices) {
            newOffsets[resolved.size + i] = blob.size
            blob = blob + authorityEncoded[i]
        }
        val newStringCount = stringCount + resolved.size + authorityEncoded.size
        val newStringsStart = spHeaderSize + newStringCount * 4
        var newChunkSize = newStringsStart + blob.size
        val pad = (4 - (newChunkSize % 4)) % 4
        if (pad > 0) {
            blob = blob + ByteArray(pad)
            newChunkSize += pad
        }

        val newSp = ByteBuffer.allocate(newChunkSize).order(ByteOrder.LITTLE_ENDIAN)
        newSp.putShort(CHUNK_STRING_POOL.toShort())
        newSp.putShort(spHeaderSize.toShort())
        newSp.putInt(newChunkSize)
        newSp.putInt(newStringCount)
        newSp.putInt(0)          // styleCount
        newSp.putInt(flags)
        newSp.putInt(newStringsStart)
        newSp.putInt(0)          // stylesStart (لا يوجد أنماط)
        for (i in 0 until stringCount) {
            newSp.putInt(buf.getInt(offsetsBase + i * 4)) // إزاحات السلاسل الأصلية لم تتغيّر
        }
        for (o in newOffsets) newSp.putInt(o)
        newSp.put(blob)

        // --- 4) تجميع الملف النهائي: رأس XML(8) + مجمّع السلاسل الجديد + بقية الملف كما هي ---
        val delta = newChunkSize - spChunkSize
        val out = ByteArrayOutputStream(totalSize + delta)
        out.write(bytes, 0, 4)
        out.write(intLE(totalSize + delta))
        out.write(newSp.array())
        out.write(bytes, spOffset + spChunkSize, bytes.size - (spOffset + spChunkSize))

        return Result(
            bytes = out.toByteArray(),
            appliedCount = resolved.size + authorityEncoded.size + appliedInt.count { it },
            intAppliedCount = appliedInt.count { it }
        )
    }

    private fun intLE(v: Int): ByteArray =
        byteArrayOf((v and 0xFF).toByte(), ((v shr 8) and 0xFF).toByte(), ((v shr 16) and 0xFF).toByte(), ((v shr 24) and 0xFF).toByte())

    private fun readUtf16(buf: ByteBuffer, off: Int): String {
        val len = buf.getShort(off).toInt() and 0xFFFF
        val sb = StringBuilder(len)
        var p = off + 2
        repeat(len) { sb.append(buf.getChar(p)); p += 2 }
        return sb.toString()
    }

    private fun encodeUtf16(s: String): ByteArray {
        val bb = ByteBuffer.allocate(2 + s.length * 2 + 2).order(ByteOrder.LITTLE_ENDIAN)
        bb.putShort(s.length.toShort())
        for (c in s) bb.putChar(c)
        bb.putShort(0)
        return bb.array()
    }

    private fun readLen8(buf: ByteBuffer, pos: Int): Pair<Int, Int> {
        val b0 = buf.get(pos).toInt() and 0xFF
        return if (b0 and 0x80 == 0) b0 to 1
        else {
            val b1 = buf.get(pos + 1).toInt() and 0xFF
            (((b0 and 0x7F) shl 8) or b1) to 2
        }
    }

    private fun readUtf8(buf: ByteBuffer, off: Int): String {
        val (_, n1) = readLen8(buf, off)               // طول UTF-16 (غير مستخدم للفك)
        val (byteLen, n2) = readLen8(buf, off + n1)
        val start = off + n1 + n2
        val out = ByteArray(byteLen)
        for (i in 0 until byteLen) out[i] = buf.get(start + i)
        return String(out, Charsets.UTF_8)
    }

    private fun encLen8(n: Int): ByteArray =
        if (n < 0x80) byteArrayOf(n.toByte())
        else byteArrayOf(((n shr 8) or 0x80).toByte(), (n and 0xFF).toByte())

    private fun encodeUtf8(s: String): ByteArray {
        val strBytes = s.toByteArray(Charsets.UTF_8)
        return encLen8(s.length) + encLen8(strBytes.size) + strBytes + byteArrayOf(0)
    }
}
