package com.dlof.rinlang.store.projects

import com.dlof.rinlang.auth.FirebaseDbConfig
import com.google.firebase.database.DataSnapshot
import com.google.firebase.database.DatabaseError
import com.google.firebase.database.FirebaseDatabase
import com.google.firebase.database.ValueEventListener

/**
 * كل عمليات "معرض مشاريع Rin" على Realtime Database (خطة Spark المجانية)، بنفس فلسفة
 * [com.dlof.rinlang.store.PackageRepository] و[com.dlof.rinlang.store.extensions.ExtensionRepository]:
 * القراءة عامة (يتصفّح أي شخص المشاريع المنشورة بلا تسجيل دخول)، والكتابة مقصورة على صاحب
 * المشروع نفسه فقط (راجع firebase/database.rules.json، عقدة "rin_projects").
 *
 * بخلاف نشر إضافة (راجع [com.dlof.rinlang.store.PublishPolicy.MIN_FOLLOWERS_TO_PUBLISH_EXTENSION])،
 * لا يوجد أي قيد على عدد المتابعين لنشر مشروع هنا — أي مستخدم مسجَّل دخوله يمكنه مشاركة أي
 * مشروع أنشأه.
 */
object RinProjectRepository {

    private val db: FirebaseDatabase
        get() = FirebaseDatabase.getInstance(FirebaseDbConfig.DATABASE_URL)

    private fun projectsRef() = db.getReference("rin_projects")

    /** يجلب كل المشاريع المنشورة، الأحدث أولاً. تحذير: يقرأ كامل حقل zipBase64 لكل مشروع. */
    fun fetchAll(callback: (List<RinProject>) -> Unit) {
        projectsRef().addListenerForSingleValueEvent(object : ValueEventListener {
            override fun onDataChange(snapshot: DataSnapshot) {
                val list = snapshot.children.mapNotNull { it.getValue(RinProject::class.java) }
                    .sortedByDescending { it.createdAt }
                callback(list)
            }

            override fun onCancelled(error: DatabaseError) = callback(emptyList())
        })
    }

    /** يجلب مشاريع الناشر [uid] فقط، الأحدث أولاً — تُستخدَم في "مشاريعي المنشورة". */
    fun fetchByPublisher(uid: String, callback: (List<RinProject>) -> Unit) {
        projectsRef().orderByChild("publisherUid").equalTo(uid)
            .addListenerForSingleValueEvent(object : ValueEventListener {
                override fun onDataChange(snapshot: DataSnapshot) {
                    val list = snapshot.children.mapNotNull { it.getValue(RinProject::class.java) }
                        .sortedByDescending { it.createdAt }
                    callback(list)
                }

                override fun onCancelled(error: DatabaseError) = callback(emptyList())
            })
    }

    fun publishProject(project: RinProject, callback: (success: Boolean, error: String?, published: RinProject?) -> Unit) {
        val ref = projectsRef().push()
        val id = ref.key ?: return callback(false, "تعذّر إنشاء معرّف للمشروع", null)
        val payload = project.copy(
            id = id,
            createdAt = System.currentTimeMillis(),
            sizeBytes = project.zipBase64.length * 3L / 4,
            downloadCount = 0L,
            likeCount = 0L
        )
        ref.setValue(payload)
            .addOnSuccessListener { callback(true, null, payload) }
            .addOnFailureListener { e -> callback(false, e.message, null) }
    }

    /** يحذف مشروعاً منشوراً [projectId] نهائياً من المعرض، بعد التحقق أن [uid] هو ناشره فعلاً. */
    fun deleteProject(projectId: String, uid: String, callback: (success: Boolean, error: String?) -> Unit) {
        val ref = projectsRef().child(projectId)
        ref.addListenerForSingleValueEvent(object : ValueEventListener {
            override fun onDataChange(snapshot: DataSnapshot) {
                val project = snapshot.getValue(RinProject::class.java)
                if (project == null) {
                    callback(false, "المشروع غير موجود")
                    return
                }
                if (project.publisherUid != uid) {
                    callback(false, "لا يمكنك حذف مشروع لا يخصّك")
                    return
                }
                ref.removeValue()
                    .addOnSuccessListener { callback(true, null) }
                    .addOnFailureListener { e -> callback(false, e.message) }
            }

            override fun onCancelled(error: DatabaseError) = callback(false, error.message)
        })
    }

    fun incrementDownloadCount(projectId: String) {
        val ref = projectsRef().child(projectId).child("downloadCount")
        ref.addListenerForSingleValueEvent(object : ValueEventListener {
            override fun onDataChange(snapshot: DataSnapshot) {
                val current = snapshot.getValue(Long::class.java) ?: 0L
                ref.setValue(current + 1)
            }

            override fun onCancelled(error: DatabaseError) {}
        })
    }

    /** يجلب هل أعجب [uid] بمشروع [projectId] مسبقاً أم لا. */
    fun fetchLikeState(projectId: String, uid: String, callback: (liked: Boolean) -> Unit) {
        projectsRef().child(projectId).child("likes").child(uid)
            .addListenerForSingleValueEvent(object : ValueEventListener {
                override fun onDataChange(snapshot: DataSnapshot) = callback(snapshot.exists())
                override fun onCancelled(error: DatabaseError) = callback(false)
            })
    }

    /** يبدّل إعجاب [uid] بمشروع [projectId]، بنفس منطق [com.dlof.rinlang.store.PackageRepository.toggleLike]. */
    fun toggleLike(projectId: String, uid: String, callback: (liked: Boolean, success: Boolean) -> Unit) {
        val likeRef = projectsRef().child(projectId).child("likes").child(uid)
        likeRef.addListenerForSingleValueEvent(object : ValueEventListener {
            override fun onDataChange(snapshot: DataSnapshot) {
                val alreadyLiked = snapshot.exists()
                val newState = !alreadyLiked
                val writeTask = if (newState) likeRef.setValue(true) else likeRef.removeValue()
                writeTask
                    .addOnSuccessListener {
                        val countRef = projectsRef().child(projectId).child("likeCount")
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
