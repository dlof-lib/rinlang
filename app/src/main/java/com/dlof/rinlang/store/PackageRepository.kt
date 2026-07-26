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
            "base64Data" to base64Data
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
}
