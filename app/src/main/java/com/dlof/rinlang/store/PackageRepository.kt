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
            "dependencies" to dependencies
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
}
