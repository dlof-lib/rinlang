package com.dlof.rinlang.store

import com.dlof.rinlang.auth.FirebaseDbConfig
import com.google.firebase.database.DataSnapshot
import com.google.firebase.database.DatabaseError
import com.google.firebase.database.FirebaseDatabase
import com.google.firebase.database.ValueEventListener

/**
 * كل عمليات "متجر Rin" على Realtime Database (خطة Spark المجانية). القراءة عامة (يتصفح
 * أي شخص الحزم بلا تسجيل دخول)، والكتابة مقصورة على صاحب الحزمة فقط (راجع
 * firebase/database.rules.json).
 */
object PackageRepository {

    private val db: FirebaseDatabase
        get() = FirebaseDatabase.getInstance(FirebaseDbConfig.DATABASE_URL)

    private fun packagesRef() = db.getReference("packages")

    /**
     * يجلب حزم المستخدم [uid] فقط (التي نشرها بنفسه)، الأحدث أولاً، عبر استعلام
     * على publisherUid بدل قراءة كل الحزم وتصفيتها محلياً (أخف على القراءة من Realtime Database).
     */
    fun fetchUserPackages(uid: String, callback: (List<RinPackage>) -> Unit) {
        packagesRef().orderByChild("publisherUid").equalTo(uid)
            .addListenerForSingleValueEvent(object : ValueEventListener {
                override fun onDataChange(snapshot: DataSnapshot) {
                    val list = snapshot.children.mapNotNull { it.getValue(RinPackage::class.java) }
                        .sortedByDescending { it.createdAt }
                    callback(list)
                }

                override fun onCancelled(error: DatabaseError) = callback(emptyList())
            })
    }

    /**
     * يحذف حزمة [packageId] نهائياً من المتجر. يتحقق أولاً أن [uid] هو ناشرها الفعلي قبل محاولة
     * الحذف (بالإضافة إلى قاعدة الأمان المطابقة على مستوى Firebase نفسها في database.rules.json،
     * التي تمنع أي حذف/تعديل لا يقوم به صاحب الحزمة أصلاً).
     */
    fun deletePackage(packageId: String, uid: String, callback: (success: Boolean, error: String?) -> Unit) {
        val ref = packagesRef().child(packageId)
        ref.addListenerForSingleValueEvent(object : ValueEventListener {
            override fun onDataChange(snapshot: DataSnapshot) {
                val pkg = snapshot.getValue(RinPackage::class.java)
                if (pkg == null) {
                    callback(false, "الحزمة غير موجودة")
                    return
                }
                if (pkg.publisherUid != uid) {
                    callback(false, "لا يمكنك حذف حزمة لا تخصّك")
                    return
                }
                ref.removeValue()
                    .addOnSuccessListener { callback(true, null) }
                    .addOnFailureListener { e -> callback(false, e.message) }
            }

            override fun onCancelled(error: DatabaseError) = callback(false, error.message)
        })
    }

    /**
     * يطبِّع اسم الحزمة لمقارنة "تشابه" لا مطابقة حرفية فقط: يُهمَل حجم الأحرف والمسافات
     * والشرطات (-) والشرطات السفلية (_) والنقاط (.)، بحيث تُعتبر "My Lib" و"my-lib" و"MY_LIB"
     * نفس الاسم فعلياً ويُمنع تكرارها.
     */
    private fun normalizeName(name: String): String =
        name.trim().lowercase().replace(Regex("[\\s\\-_.]+"), "")

    /**
     * يتحقق أن اسم الحزمة [name] غير مستخدَم من قِبل أي ناشر آخر في متجر Rin بالكامل (وليس فقط
     * حزم المستخدم الحالي)، لمنع تشابه الأسماء بين كل المستخدمين. [excludePackageId] يُستخدم عند
     * تعديل حزمة موجودة مسبقاً حتى لا تُقارَن الحزمة بنفسها.
     */
    fun isNameAvailable(name: String, excludePackageId: String? = null, callback: (Boolean) -> Unit) {
        val target = normalizeName(name)
        if (target.isEmpty()) { callback(false); return }
        packagesRef().addListenerForSingleValueEvent(object : ValueEventListener {
            override fun onDataChange(snapshot: DataSnapshot) {
                val taken = snapshot.children.any { child ->
                    val pkg = child.getValue(RinPackage::class.java) ?: return@any false
                    pkg.id != excludePackageId && normalizeName(pkg.name) == target
                }
                callback(!taken)
            }

            override fun onCancelled(error: DatabaseError) = callback(true) // تعذّر التحقق: لا نمنع النشر بسبب خطأ شبكة
        })
    }

    /**
     * يجلب كل الإصدارات المنشورة سابقاً باسم مطابق لـ [name] بعد التطبيع (راجع [normalizeName])،
     * بغضّ النظر عن ناشرها — تُستخدَم لتطبيق سياسة [PublishPolicy.validateVersionProgression]
     * (يجب أن يكون كل إصدار جديد أحدث من كل ما نُشر سابقاً بهذا الاسم). قراءة واحدة فقط على
     * كامل packages/، بنفس تكلفة [isNameAvailable] تقريباً.
     */
    fun fetchExistingVersions(name: String, callback: (List<String>) -> Unit) {
        val target = normalizeName(name)
        if (target.isEmpty()) { callback(emptyList()); return }
        packagesRef().addListenerForSingleValueEvent(object : ValueEventListener {
            override fun onDataChange(snapshot: DataSnapshot) {
                val versions = snapshot.children.mapNotNull { child ->
                    val pkg = child.getValue(RinPackage::class.java) ?: return@mapNotNull null
                    if (normalizeName(pkg.name) == target) pkg.version else null
                }
                callback(versions)
            }

            override fun onCancelled(error: DatabaseError) = callback(emptyList())
        })
    }

    fun publishPackage(
        name: String,
        version: String,
        description: String,
        license: String,
        publisherUid: String,
        publisherName: String,
        fileName: String,
        base64Data: String,
        category: String = "عام",
        dependencies: Map<String, String> = emptyMap(),
        /** أيقونة الحزمة (اختيارية) مصغَّرة ومرمَّزة base64 مسبقاً — راجع [RinPackage.iconBase64]. */
        iconBase64: String = "",
        /** صورة مصغّرة للحزمة (اختيارية) — راجع [RinPackage.thumbnailBase64]. */
        thumbnailBase64: String = "",
        callback: (success: Boolean, error: String?) -> Unit
    ) {
        val ref = packagesRef().push()
        val id = ref.key ?: return callback(false, "تعذّر إنشاء معرّف للحزمة")

        val payload = mapOf(
            "id" to id,
            "name" to name,
            "version" to version,
            "description" to description,
            "license" to license,
            "publisherUid" to publisherUid,
            "publisherName" to publisherName,
            "fileName" to fileName,
            "sizeBytes" to (base64Data.length * 3L / 4),
            "downloadCount" to 0L,
            "createdAt" to System.currentTimeMillis(),
            "base64Data" to base64Data,
            "category" to category.ifBlank { "عام" },
            "ratingSum" to 0L,
            "ratingCount" to 0L,
            "likeCount" to 0L,
            "dependencies" to dependencies,
            "iconBase64" to iconBase64,
            "thumbnailBase64" to thumbnailBase64
        )

        ref.setValue(payload)
            .addOnSuccessListener { callback(true, null) }
            .addOnFailureListener { e -> callback(false, e.message) }
    }

    /** يجلب كل الحزم المنشورة، الأحدث أولاً. تحذير: يقرأ كامل الحقل base64Data لكل حزمة. */
    fun fetchAllPackages(callback: (List<RinPackage>) -> Unit) {
        packagesRef().addListenerForSingleValueEvent(object : ValueEventListener {
            override fun onDataChange(snapshot: DataSnapshot) {
                val list = snapshot.children.mapNotNull { it.getValue(RinPackage::class.java) }
                    .sortedByDescending { it.createdAt }
                callback(list)
            }

            override fun onCancelled(error: DatabaseError) = callback(emptyList())
        })
    }

    fun incrementDownloadCount(packageId: String) {
        val ref = packagesRef().child(packageId).child("downloadCount")
        ref.addListenerForSingleValueEvent(object : ValueEventListener {
            override fun onDataChange(snapshot: DataSnapshot) {
                val current = snapshot.getValue(Long::class.java) ?: 0L
                ref.setValue(current + 1)
            }

            override fun onCancelled(error: DatabaseError) {}
        })
    }

    /** يجلب تقييم [uid] الحالي لهذه الحزمة (1-5)، أو null إن لم يقيّمها بعد. */
    fun fetchUserRating(packageId: String, uid: String, callback: (Int?) -> Unit) {
        packagesRef().child(packageId).child("ratings").child(uid)
            .addListenerForSingleValueEvent(object : ValueEventListener {
                override fun onDataChange(snapshot: DataSnapshot) =
                    callback(snapshot.getValue(Long::class.java)?.toInt())

                override fun onCancelled(error: DatabaseError) = callback(null)
            })
    }

    /**
     * يسجّل/يحدّث تقييم [uid] لحزمة [packageId] بقيمة [value] (1-5)، ويعدّل مجموع/عدد
     * التقييمات على مستوى الحزمة عبر transaction (يعوّض تقييماً سابقاً لنفس المستخدم إن وُجد
     * بدل مضاعفته في المجموع).
     */
    fun submitRating(packageId: String, uid: String, value: Int, callback: (Boolean) -> Unit) {
        val ratingRef = packagesRef().child(packageId).child("ratings").child(uid)
        ratingRef.addListenerForSingleValueEvent(object : ValueEventListener {
            override fun onDataChange(snapshot: DataSnapshot) {
                val previous = snapshot.getValue(Long::class.java)?.toInt()
                ratingRef.setValue(value)
                    .addOnSuccessListener {
                        val pkgRef = packagesRef().child(packageId)
                        pkgRef.child("ratingSum").addListenerForSingleValueEvent(object : ValueEventListener {
                            override fun onDataChange(sumSnap: DataSnapshot) {
                                val currentSum = sumSnap.getValue(Long::class.java) ?: 0L
                                val newSum = currentSum - (previous ?: 0) + value
                                pkgRef.child("ratingSum").setValue(newSum)
                                if (previous == null) {
                                    pkgRef.child("ratingCount").addListenerForSingleValueEvent(object : ValueEventListener {
                                        override fun onDataChange(countSnap: DataSnapshot) {
                                            val currentCount = countSnap.getValue(Long::class.java) ?: 0L
                                            pkgRef.child("ratingCount").setValue(currentCount + 1)
                                        }
                                        override fun onCancelled(error: DatabaseError) {}
                                    })
                                }
                                callback(true)
                            }
                            override fun onCancelled(error: DatabaseError) = callback(false)
                        })
                    }
                    .addOnFailureListener { callback(false) }
            }
            override fun onCancelled(error: DatabaseError) = callback(false)
        })
    }

    /**
     * يبلّغ [uid] عن حزمة [packageId] بسبب [reason] (بلاغ واحد لكل مستخدم لكل حزمة —
     * يستبدل بلاغه السابق إن كرّر الإبلاغ بدل مضاعفة العدّاد). يُزاد reportCount فقط
     * عند أول بلاغ من هذا المستخدم لهذه الحزمة.
     */
    fun submitReport(packageId: String, uid: String, reason: String, callback: (Boolean) -> Unit) {
        val reportRef = packagesRef().child(packageId).child("reports").child(uid)
        reportRef.addListenerForSingleValueEvent(object : ValueEventListener {
            override fun onDataChange(snapshot: DataSnapshot) {
                val alreadyReported = snapshot.exists()
                val payload = mapOf(
                    "reason" to reason,
                    "reportedAt" to System.currentTimeMillis()
                )
                reportRef.setValue(payload)
                    .addOnSuccessListener {
                        if (alreadyReported) {
                            callback(true)
                            return@addOnSuccessListener
                        }
                        val countRef = packagesRef().child(packageId).child("reportCount")
                        countRef.addListenerForSingleValueEvent(object : ValueEventListener {
                            override fun onDataChange(countSnap: DataSnapshot) {
                                val current = countSnap.getValue(Long::class.java) ?: 0L
                                countRef.setValue(current + 1)
                                callback(true)
                            }
                            override fun onCancelled(error: DatabaseError) = callback(true)
                        })
                    }
                    .addOnFailureListener { callback(false) }
            }
            override fun onCancelled(error: DatabaseError) = callback(false)
        })
    }

    /** يجلب هل أعجب [uid] بحزمة [packageId] مسبقاً أم لا (لضبط شكل زر القلب عند فتح الشاشة). */
    fun fetchLikeState(packageId: String, uid: String, callback: (liked: Boolean) -> Unit) {
        packagesRef().child(packageId).child("likes").child(uid)
            .addListenerForSingleValueEvent(object : ValueEventListener {
                override fun onDataChange(snapshot: DataSnapshot) = callback(snapshot.exists())
                override fun onCancelled(error: DatabaseError) = callback(false)
            })
    }

    /**
     * يبدّل إعجاب [uid] بحزمة [packageId]: يزيل الإعجاب وينقص likeCount إن كان معجَباً بها
     * مسبقاً، أو يسجّله ويزيد likeCount إن لم يكن. يُعيد عبر [callback] الحالة الجديدة (liked)
     * ونجاح العملية من عدمه، حتى تُحدَّث الواجهة فوراً بشكل متفائل (optimistic) أو تتراجع عند الفشل.
     */
    fun toggleLike(packageId: String, uid: String, callback: (liked: Boolean, success: Boolean) -> Unit) {
        val likeRef = packagesRef().child(packageId).child("likes").child(uid)
        likeRef.addListenerForSingleValueEvent(object : ValueEventListener {
            override fun onDataChange(snapshot: DataSnapshot) {
                val alreadyLiked = snapshot.exists()
                val newState = !alreadyLiked
                val writeTask = if (newState) likeRef.setValue(true) else likeRef.removeValue()
                writeTask
                    .addOnSuccessListener {
                        val countRef = packagesRef().child(packageId).child("likeCount")
                        countRef.addListenerForSingleValueEvent(object : ValueEventListener {
                            override fun onDataChange(countSnap: DataSnapshot) {
                                val current = countSnap.getValue(Long::class.java) ?: 0L
                                val updated = if (newState) current + 1 else (current - 1).coerceAtLeast(0L)
                                countRef.setValue(updated)
                                callback(newState, true)
                            }
                            override fun onCancelled(error: DatabaseError) = callback(alreadyLiked, false)
                        })
                    }
                    .addOnFailureListener { callback(alreadyLiked, false) }
            }
            override fun onCancelled(error: DatabaseError) = callback(false, false)
        })
    }
}
