package com.dlof.rinlang.store

import android.app.AlertDialog
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.net.Uri
import android.os.Bundle
import android.util.Base64
import android.view.LayoutInflater
import android.view.View
import android.widget.EditText
import android.widget.ImageView
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import com.dlof.rinlang.network.BaseConnectivityActivity
import com.dlof.rinlang.R
import com.dlof.rinlang.auth.AuthRepository
import com.dlof.rinlang.auth.LoginActivity
import com.dlof.rinlang.auth.RinUser
import java.io.ByteArrayOutputStream

/**
 * شاشة "حسابي": ملف شخصي احترافي (صورة دائرية + اسم + اسم مستخدم + نبذة)، تعديل الاسم/النبذة،
 * تغيير صورة الملف الشخصي (تُصغَّر وتُضغط ثم تُرمَّز base64 — بلا Firebase Storage مدفوع، بنفس
 * أسلوب حزم متجر Rin)، أو تسجيل الخروج.
 */
class AccountActivity : BaseConnectivityActivity() {

    companion object {
        /** أقصى بُعد (طول/عرض) للصورة بعد التصغير قبل الترميز، لإبقاء حجمها معقولاً داخل Realtime Database. */
        private const val AVATAR_MAX_DIMENSION = 512
        private const val AVATAR_JPEG_QUALITY = 82
    }

    private var currentProfile: RinUser? = null

    private val pickAvatarLauncher =
        registerForActivityResult(ActivityResultContracts.GetContent()) { uri ->
            if (uri != null) handlePickedAvatar(uri)
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_account)

        findViewById<TextView>(R.id.txtToolbarTitle).text = getString(R.string.account_title)
        findViewById<View>(R.id.btnToolbarBack).setOnClickListener { finish() }

        refresh()
    }

    /** يُستدعى تلقائياً عند عودة الاتصال بعد انقطاعه أثناء وجود المستخدم في شاشة الحساب. */
    override fun onConnectionRestored() {
        refresh()
    }

    private fun refresh() {
        val uid = AuthRepository.currentUid()
        val loggedInGroup = findViewById<View>(R.id.groupLoggedIn)
        val loggedOutGroup = findViewById<View>(R.id.groupLoggedOut)

        if (uid == null) {
            loggedInGroup.visibility = View.GONE
            loggedOutGroup.visibility = View.VISIBLE
            findViewById<View>(R.id.btnGoLogin).setOnClickListener {
                startActivity(Intent(this, LoginActivity::class.java))
            }
            return
        }

        loggedInGroup.visibility = View.VISIBLE
        loggedOutGroup.visibility = View.GONE

        AuthRepository.fetchProfile(uid) { profile ->
            currentProfile = profile
            bindProfile(profile)
        }

        findViewById<View>(R.id.btnChangeAvatar).setOnClickListener {
            pickAvatarLauncher.launch("image/*")
        }

        findViewById<View>(R.id.btnEditProfile).setOnClickListener {
            showEditProfileDialog(uid)
        }

        findViewById<View>(R.id.btnLogout).setOnClickListener {
            AuthRepository.logout()
            refresh()
        }
    }

    private fun bindProfile(profile: RinUser?) {
        val displayName = profile?.name?.ifBlank { profile.username } ?: "—"
        findViewById<TextView>(R.id.txtAccountName).text = displayName
        findViewById<TextView>(R.id.txtAccountUsername).text = "@${profile?.username ?: ""}"
        findViewById<TextView>(R.id.txtAccountEmail).text = profile?.email ?: ""

        val txtBio = findViewById<TextView>(R.id.txtAccountBio)
        if (profile?.bio.isNullOrBlank()) {
            txtBio.text = getString(R.string.account_bio_placeholder)
            txtBio.alpha = 0.7f
        } else {
            txtBio.text = profile?.bio
            txtBio.alpha = 1f
        }

        renderAvatar(profile, displayName)
    }

    /** يعرض صورة الملف الشخصي إن وُجدت (فكّ base64 وعرضها)، وإلا يعرض شارة بحرف الاسم الأول. */
    private fun renderAvatar(profile: RinUser?, displayName: String) {
        val imgAvatar = findViewById<ImageView>(R.id.imgAvatar)
        val txtInitial = findViewById<TextView>(R.id.txtAvatarInitial)

        clipToCircle(imgAvatar)

        val avatarBase64 = profile?.avatarBase64
        if (avatarBase64.isNullOrBlank()) {
            imgAvatar.setImageDrawable(null)
            txtInitial.text = displayName.take(1).uppercase()
            txtInitial.visibility = View.VISIBLE
        } else {
            try {
                val bytes = Base64.decode(avatarBase64, Base64.NO_WRAP)
                val bitmap = BitmapFactory.decodeByteArray(bytes, 0, bytes.size)
                if (bitmap != null) {
                    imgAvatar.setImageBitmap(bitmap)
                    txtInitial.visibility = View.GONE
                } else {
                    txtInitial.text = displayName.take(1).uppercase()
                    txtInitial.visibility = View.VISIBLE
                }
            } catch (t: Throwable) {
                txtInitial.text = displayName.take(1).uppercase()
                txtInitial.visibility = View.VISIBLE
            }
        }
    }

    private fun clipToCircle(view: ImageView) {
        view.clipToOutline = true
        view.outlineProvider = object : android.view.ViewOutlineProvider() {
            override fun getOutline(v: View, outline: android.graphics.Outline) {
                outline.setOval(0, 0, v.width, v.height)
            }
        }
    }

    /** يقرأ الصورة المُختارة، يصغّرها إلى [AVATAR_MAX_DIMENSION] كحد أقصى، يضغطها JPEG، ثم يرفعها كـ base64. */
    private fun handlePickedAvatar(uri: Uri) {
        val uid = AuthRepository.currentUid() ?: return
        if (!isOnline()) { showOfflineOverlay(); return }
        try {
            val input = contentResolver.openInputStream(uri) ?: return
            val original = input.use { BitmapFactory.decodeStream(it) } ?: run {
                Toast.makeText(this, R.string.avatar_update_failed, Toast.LENGTH_SHORT).show()
                return
            }
            val resized = resizeBitmap(original, AVATAR_MAX_DIMENSION)
            val outputStream = ByteArrayOutputStream()
            resized.compress(Bitmap.CompressFormat.JPEG, AVATAR_JPEG_QUALITY, outputStream)
            val base64 = Base64.encodeToString(outputStream.toByteArray(), Base64.NO_WRAP)

            AuthRepository.updateAvatar(uid, base64) { success ->
                if (success) {
                    Toast.makeText(this, R.string.avatar_updated, Toast.LENGTH_SHORT).show()
                    refresh()
                } else {
                    Toast.makeText(this, R.string.avatar_update_failed, Toast.LENGTH_SHORT).show()
                }
            }
        } catch (t: Throwable) {
            Toast.makeText(this, R.string.avatar_update_failed, Toast.LENGTH_SHORT).show()
        }
    }

    /** يصغّر [source] بحيث لا يتجاوز أي بُعد [maxDimension]، مع الحفاظ على النسبة. */
    private fun resizeBitmap(source: Bitmap, maxDimension: Int): Bitmap {
        val width = source.width
        val height = source.height
        if (width <= maxDimension && height <= maxDimension) return source
        val scale = maxDimension.toFloat() / maxOf(width, height)
        val newWidth = (width * scale).toInt().coerceAtLeast(1)
        val newHeight = (height * scale).toInt().coerceAtLeast(1)
        return Bitmap.createScaledBitmap(source, newWidth, newHeight, true)
    }

    private fun showEditProfileDialog(uid: String) {
        val view = LayoutInflater.from(this).inflate(R.layout.dialog_edit_profile, null)
        val edtName = view.findViewById<EditText>(R.id.edtProfileName)
        val edtBio = view.findViewById<EditText>(R.id.edtProfileBio)
        edtName.setText(currentProfile?.name.orEmpty())
        edtBio.setText(currentProfile?.bio.orEmpty())

        AlertDialog.Builder(this)
            .setTitle(R.string.edit_profile_title)
            .setView(view)
            .setPositiveButton(R.string.action_save) { _, _ ->
                if (!isOnline()) { showOfflineOverlay(); return@setPositiveButton }
                val name = edtName.text.toString().trim()
                val bio = edtBio.text.toString().trim()
                AuthRepository.updateProfile(uid, name, bio) { success ->
                    if (success) {
                        Toast.makeText(this, R.string.profile_updated, Toast.LENGTH_SHORT).show()
                        refresh()
                    }
                }
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }
}
