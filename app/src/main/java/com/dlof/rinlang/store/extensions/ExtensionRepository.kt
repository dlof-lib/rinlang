package com.dlof.rinlang.store.extensions

import com.dlof.rinlang.auth.FirebaseDbConfig
import com.google.firebase.database.DataSnapshot
import com.google.firebase.database.DatabaseError
import com.google.firebase.database.FirebaseDatabase
import com.google.firebase.database.ValueEventListener

/**
 * كل عمليات "Rin Extensions Marketplace" على Realtime Database (نفس خطة Spark المجانية
 * المستخدَمة في [com.dlof.rinlang.store.PackageRepository]). القراءة عامة (يتصفّح أي شخص
 * الإضافات بلا تسجيل دخول)، والكتابة مقصورة على مطوّر الإضافة نفسه فقط
 * (راجع firebase/database.rules.json، عقدة "extensions").
 */
object ExtensionRepository {

    private val db: FirebaseDatabase
        get() = FirebaseDatabase.getInstance(FirebaseDbConfig.DATABASE_URL)

    private fun extensionsRef() = db.getReference("extensions")

    /** يجلب كل الإضافات المنشورة، الأحدث أولاً. */
    fun fetchAll(callback: (List<RinExtension>) -> Unit) {
        extensionsRef().addListenerForSingleValueEvent(object : ValueEventListener {
            override fun onDataChange(snapshot: DataSnapshot) {
                val list = snapshot.children.mapNotNull { it.getValue(RinExtension::class.java) }
                    .sortedByDescending { it.releaseDate }
                callback(list)
            }

            override fun onCancelled(error: DatabaseError) = callback(emptyList())
        })
    }

    /** يجلب إضافات مطوّر [uid] فقط، الأحدث أولاً. */
    fun fetchByDeveloper(uid: String, callback: (List<RinExtension>) -> Unit) {
        extensionsRef().orderByChild("developerUid").equalTo(uid)
            .addListenerForSingleValueEvent(object : ValueEventListener {
                override fun onDataChange(snapshot: DataSnapshot) {
                    val list = snapshot.children.mapNotNull { it.getValue(RinExtension::class.java) }
                        .sortedByDescending { it.releaseDate }
                    callback(list)
                }

                override fun onCancelled(error: DatabaseError) = callback(emptyList())
            })
    }

    /** يجلب إضافة واحدة بمعرّفها، أو null إن لم توجد. */
    fun fetchById(extensionId: String, callback: (RinExtension?) -> Unit) {
        extensionsRef().child(extensionId).addListenerForSingleValueEvent(object : ValueEventListener {
            override fun onDataChange(snapshot: DataSnapshot) = callback(snapshot.getValue(RinExtension::class.java))
            override fun onCancelled(error: DatabaseError) = callback(null)
        })
    }

    fun publishExtension(
        ext: RinExtension,
        callback: (success: Boolean, error: String?, published: RinExtension?) -> Unit
    ) {
        val ref = extensionsRef().push()
        val id = ref.key ?: return callback(false, "تعذّر إنشاء معرّف للإضافة", null)
        val signature = ExtensionPermissions.computeSignature(ext.base64Data)
        val payload = ext.copy(
            id = id,
            signature = signature,
            sizeBytes = ext.base64Data.length * 3L / 4,
            releaseDate = System.currentTimeMillis(),
            downloadCount = 0L,
            ratingSum = 0L,
            ratingCount = 0L,
            likeCount = 0L,
            reportCount = 0L
        )
        ref.setValue(payload)
            .addOnSuccessListener { callback(true, null, payload) }
            .addOnFailureListener { e -> callback(false, e.message, null) }
    }

    /**
     * يحذف إضافة [extensionId] نهائياً من "Rin Extensions Marketplace". يتحقق أولاً أن [uid]
     * هو مطوّرها الفعلي (developerUid) قبل محاولة الحذف — بالإضافة إلى قاعدة الأمان المطابقة على
     * مستوى Firebase نفسها في firebase/database.rules.json التي تمنع أصلاً أي كتابة/حذف لا يقوم
     * بها صاحب الإضافة. هذا الحذف يزيل الإضافة من المتجر فقط، ولا يمسّ نسخها المثبَّتة محلياً على
     * أجهزة من ثبّتوها من قبل (راجع [ExtensionManager.uninstall] لذلك).
     */
    fun deleteExtension(extensionId: String, uid: String, callback: (success: Boolean, error: String?) -> Unit) {
        val ref = extensionsRef().child(extensionId)
        ref.addListenerForSingleValueEvent(object : ValueEventListener {
            override fun onDataChange(snapshot: DataSnapshot) {
                val ext = snapshot.getValue(RinExtension::class.java)
                if (ext == null) {
                    callback(false, "الإضافة غير موجودة")
                    return
                }
                if (ext.developerUid != uid) {
                    callback(false, "لا يمكنك حذف إضافة لا تخصّك")
                    return
                }
                ref.removeValue()
                    .addOnSuccessListener { callback(true, null) }
                    .addOnFailureListener { e -> callback(false, e.message) }
            }

            override fun onCancelled(error: DatabaseError) = callback(false, error.message)
        })
    }

    fun incrementDownloadCount(extensionId: String) {
        val ref = extensionsRef().child(extensionId).child("downloadCount")
        ref.addListenerForSingleValueEvent(object : ValueEventListener {
            override fun onDataChange(snapshot: DataSnapshot) {
                val current = snapshot.getValue(Long::class.java) ?: 0L
                ref.setValue(current + 1)
            }

            override fun onCancelled(error: DatabaseError) {}
        })
    }

    fun submitRating(extensionId: String, uid: String, value: Int, callback: (Boolean) -> Unit) {
        val ratingRef = extensionsRef().child(extensionId).child("ratings").child(uid)
        ratingRef.addListenerForSingleValueEvent(object : ValueEventListener {
            override fun onDataChange(snapshot: DataSnapshot) {
                val previous = snapshot.getValue(Long::class.java)?.toInt()
                ratingRef.setValue(value)
                    .addOnSuccessListener {
                        val extRef = extensionsRef().child(extensionId)
                        extRef.child("ratingSum").addListenerForSingleValueEvent(object : ValueEventListener {
                            override fun onDataChange(sumSnap: DataSnapshot) {
                                val currentSum = sumSnap.getValue(Long::class.java) ?: 0L
                                extRef.child("ratingSum").setValue(currentSum - (previous ?: 0) + value)
                                if (previous == null) {
                                    extRef.child("ratingCount").addListenerForSingleValueEvent(object : ValueEventListener {
                                        override fun onDataChange(countSnap: DataSnapshot) {
                                            val currentCount = countSnap.getValue(Long::class.java) ?: 0L
                                            extRef.child("ratingCount").setValue(currentCount + 1)
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

    fun submitReport(extensionId: String, uid: String, reason: String, callback: (Boolean) -> Unit) {
        val reportRef = extensionsRef().child(extensionId).child("reports").child(uid)
        reportRef.addListenerForSingleValueEvent(object : ValueEventListener {
            override fun onDataChange(snapshot: DataSnapshot) {
                val alreadyReported = snapshot.exists()
                reportRef.setValue(mapOf("reason" to reason, "reportedAt" to System.currentTimeMillis()))
                    .addOnSuccessListener {
                        if (alreadyReported) { callback(true); return@addOnSuccessListener }
                        val countRef = extensionsRef().child(extensionId).child("reportCount")
                        countRef.addListenerForSingleValueEvent(object : ValueEventListener {
                            override fun onDataChange(countSnap: DataSnapshot) {
                                countRef.setValue((countSnap.getValue(Long::class.java) ?: 0L) + 1)
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
