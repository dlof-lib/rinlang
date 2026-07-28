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
import android.widget.GridLayout
import android.widget.ImageButton
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import com.dlof.rinlang.BottomNavHelper
import com.dlof.rinlang.BottomNavTab
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

        /** مكتبة الأيقونات الجاهزة (نار / سحابة / قطرة ماء) المتاحة كصورة رمزية بديلة عن رفع صورة. */
        private val AVATAR_LIBRARY_ICONS = listOf(
            R.drawable.ic_avatar_fire_sunset,
            R.drawable.ic_avatar_fire_gold,
            R.drawable.ic_avatar_fire_amber,
            R.drawable.ic_avatar_fire_bronze,
            R.drawable.ic_avatar_fire_red,
            R.drawable.ic_avatar_cloud_green_glossy,
            R.drawable.ic_avatar_cloud_purple_glossy,
            R.drawable.ic_avatar_cloud_orange_glossy,
            R.drawable.ic_avatar_cloud_purple_flat,
            R.drawable.ic_avatar_cloud_yellow_flat,
            R.drawable.ic_avatar_cloud_green_flat,
            R.drawable.ic_avatar_cloud_teal_swirl,
            R.drawable.ic_avatar_cloud_gold_glossy,
            R.drawable.ic_avatar_water_indigo,
            R.drawable.ic_avatar_water_teal,
            R.drawable.ic_avatar_water_seafoam,
            R.drawable.ic_avatar_water_sky,
            R.drawable.ic_avatar_water_ocean
        )
    }

    private var currentProfile: RinUser? = null

    private val pickAvatarLauncher =
        registerForActivityResult(ActivityResultContracts.GetContent()) { uri ->
            if (uri != null) handlePickedAvatar(uri)
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_account)
        BottomNavHelper.setup(this, BottomNavTab.PROFILE)

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

        loadMyPackages(uid)
        loadMyExtensions(uid)

        findViewById<View>(R.id.btnChangeAvatar).setOnClickListener {
            showAvatarPickerDialog()
        }

        findViewById<View>(R.id.btnEditProfile).setOnClickListener {
            showEditProfileDialog(uid)
        }

        findViewById<View>(R.id.btnLogout).setOnClickListener {
            AuthRepository.logout()
            refresh()
        }

        findViewById<View>(R.id.rowAccountSubscribers).setOnClickListener {
            UserListActivity.start(this, uid, UserListActivity.Mode.SUBSCRIBERS)
        }
        findViewById<View>(R.id.rowAccountSubscriptions).setOnClickListener {
            UserListActivity.start(this, uid, UserListActivity.Mode.SUBSCRIPTIONS)
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

        findViewById<TextView>(R.id.txtAccountSubscriberCount).text = (profile?.subscriberCount ?: 0L).toString()
        findViewById<TextView>(R.id.txtAccountSubscriptionsCount).text = (profile?.subscriptionsCount ?: 0L).toString()

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

    /** يعرض نافذة اختيار الصورة الرمزية: شبكة أيقونات جاهزة + خيار رفع صورة من الاستوديو. */
    private fun showAvatarPickerDialog() {
        val view = LayoutInflater.from(this).inflate(R.layout.dialog_avatar_library, null)
        val dialog = AlertDialog.Builder(this)
            .setView(view)
            .create()

        val grid = view.findViewById<GridLayout>(R.id.gridAvatarLibrary)
        val tileSize = (resources.displayMetrics.density * 68).toInt()
        val tileMargin = (resources.displayMetrics.density * 6).toInt()

        for (resId in AVATAR_LIBRARY_ICONS) {
            val tile = ImageView(this)
            tile.setImageResource(resId)
            tile.scaleType = ImageView.ScaleType.CENTER_CROP
            tile.background = androidx.core.content.ContextCompat.getDrawable(this, R.drawable.bg_avatar_circle)
            tile.clipToOutline = true
            tile.outlineProvider = object : android.view.ViewOutlineProvider() {
                override fun getOutline(v: View, outline: android.graphics.Outline) {
                    outline.setOval(0, 0, v.width, v.height)
                }
            }
            val params = GridLayout.LayoutParams()
            params.width = tileSize
            params.height = tileSize
            params.setMargins(tileMargin, tileMargin, tileMargin, tileMargin)
            tile.layoutParams = params
            tile.setOnClickListener {
                dialog.dismiss()
                selectLibraryAvatar(resId)
            }
            grid.addView(tile)
        }

        view.findViewById<View>(R.id.btnUploadFromStudio).setOnClickListener {
            dialog.dismiss()
            pickAvatarLauncher.launch("image/*")
        }

        dialog.show()
    }

    /** يحوّل أيقونة من مكتبة الأيقونات إلى Bitmap ثم يرفعها بنفس مسار رفع صورة المستخدم. */
    private fun selectLibraryAvatar(resId: Int) {
        val uid = AuthRepository.currentUid() ?: return
        if (!isOnline()) { showOfflineOverlay(); return }
        try {
            val bitmap = BitmapFactory.decodeResource(resources, resId) ?: run {
                Toast.makeText(this, R.string.avatar_update_failed, Toast.LENGTH_SHORT).show()
                return
            }
            uploadAvatarBitmap(uid, bitmap)
        } catch (t: Throwable) {
            Toast.makeText(this, R.string.avatar_update_failed, Toast.LENGTH_SHORT).show()
        }
    }

    /** يقرأ الصورة المُختارة من الاستوديو (المعرض)، يصغّرها، ثم يرفعها بنفس مسار مكتبة الأيقونات. */
    private fun handlePickedAvatar(uri: Uri) {
        val uid = AuthRepository.currentUid() ?: return
        if (!isOnline()) { showOfflineOverlay(); return }
        try {
            val input = contentResolver.openInputStream(uri) ?: return
            val original = input.use { BitmapFactory.decodeStream(it) } ?: run {
                Toast.makeText(this, R.string.avatar_update_failed, Toast.LENGTH_SHORT).show()
                return
            }
            uploadAvatarBitmap(uid, original)
        } catch (t: Throwable) {
            Toast.makeText(this, R.string.avatar_update_failed, Toast.LENGTH_SHORT).show()
        }
    }

    /** يصغّر [source] إلى [AVATAR_MAX_DIMENSION]، يضغطه JPEG، ويرمّزه base64 ثم يرفعه لملف [uid]. مسار مشترك
     *  سواء كانت الصورة من مكتبة الأيقونات الجاهزة أو من صورة رفعها المستخدم من الاستوديو. */
    private fun uploadAvatarBitmap(uid: String, source: Bitmap) {
        val resized = resizeBitmap(source, AVATAR_MAX_DIMENSION)
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

    /** يجلب حزم المستخدم [uid] المنشورة في متجر Rin ويعرضها ضمن قسم "حزمي المنشورة"، ويحسب شارة التوثيق منها. */
    private fun loadMyPackages(uid: String) {
        PackageRepository.fetchUserPackages(uid) { packages ->
            findViewById<View>(R.id.imgAccountVerifiedBadge).visibility =
                if (PublisherBadgeUtils.isEligible(packages)) View.VISIBLE else View.GONE
            renderMyPackages(uid, packages)
        }
    }

    /** يبني صفوف حزم المستخدم داخل [R.id.containerMyPackages]، أو يعرض رسالة "لا توجد حزم" إن كانت فارغة. */
    private fun renderMyPackages(uid: String, packages: List<RinPackage>) {
        val container = findViewById<LinearLayout>(R.id.containerMyPackages)
        val txtEmpty = findViewById<TextView>(R.id.txtMyPackagesEmpty)
        container.removeAllViews()

        if (packages.isEmpty()) {
            txtEmpty.visibility = View.VISIBLE
            return
        }
        txtEmpty.visibility = View.GONE

        val inflater = LayoutInflater.from(this)
        for (pkg in packages) {
            val row = inflater.inflate(R.layout.item_my_package, container, false)
            row.findViewById<TextView>(R.id.txtMyPackageInitial).text = pkg.name.take(1).uppercase()
            row.findViewById<TextView>(R.id.txtMyPackageName).text = pkg.name
            row.findViewById<TextView>(R.id.txtMyPackageMeta).text =
                getString(R.string.my_package_meta_format, pkg.version, pkg.downloadCount)
            row.findViewById<ImageButton>(R.id.btnDeleteMyPackage).setOnClickListener {
                confirmDeletePackage(uid, pkg)
            }
            container.addView(row)
        }
    }

    /** يعرض تأكيداً قبل حذف [pkg] نهائياً (لا يمكن التراجع)، ثم يحذفها ويحدّث القائمة عند النجاح. */
    private fun confirmDeletePackage(uid: String, pkg: RinPackage) {
        AlertDialog.Builder(this)
            .setTitle(R.string.delete_package_confirm_title)
            .setMessage(getString(R.string.delete_package_confirm_message, pkg.name))
            .setPositiveButton(R.string.action_delete_package) { _, _ ->
                if (!isOnline()) { showOfflineOverlay(); return@setPositiveButton }
                PackageRepository.deletePackage(pkg.id, uid) { success, error ->
                    if (success) {
                        Toast.makeText(this, R.string.package_deleted_toast, Toast.LENGTH_SHORT).show()
                        loadMyPackages(uid)
                    } else {
                        Toast.makeText(this, error ?: getString(R.string.package_delete_failed), Toast.LENGTH_LONG).show()
                    }
                }
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    /** يجلب إضافات المستخدم [uid] المنشورة في Rin Extensions Marketplace ويعرضها ضمن قسم "إضافاتي المنشورة". */
    private fun loadMyExtensions(uid: String) {
        com.dlof.rinlang.store.extensions.ExtensionRepository.fetchByDeveloper(uid) { extensions ->
            renderMyExtensions(uid, extensions)
        }
    }

    /** يبني صفوف إضافات المستخدم داخل [R.id.containerMyExtensions]، أو يعرض رسالة "لا توجد إضافات" إن كانت فارغة. */
    private fun renderMyExtensions(uid: String, extensions: List<com.dlof.rinlang.store.extensions.RinExtension>) {
        val container = findViewById<LinearLayout>(R.id.containerMyExtensions)
        val txtEmpty = findViewById<TextView>(R.id.txtMyExtensionsEmpty)
        container.removeAllViews()

        if (extensions.isEmpty()) {
            txtEmpty.visibility = View.VISIBLE
            return
        }
        txtEmpty.visibility = View.GONE

        val inflater = LayoutInflater.from(this)
        for (ext in extensions) {
            val row = inflater.inflate(R.layout.item_my_extension, container, false)
            row.findViewById<TextView>(R.id.txtMyExtensionInitial).text = ext.name.take(1).uppercase()
            row.findViewById<TextView>(R.id.txtMyExtensionName).text = ext.name
            row.findViewById<TextView>(R.id.txtMyExtensionMeta).text =
                getString(R.string.my_extension_meta_format, ext.version, ext.downloadCount)
            row.findViewById<ImageButton>(R.id.btnDeleteMyExtension).setOnClickListener {
                confirmDeleteExtension(uid, ext)
            }
            container.addView(row)
        }
    }

    /** يعرض تأكيداً قبل حذف [ext] نهائياً من متجر Rin Extensions (لا يمكن التراجع)، ثم يحذفها ويحدّث القائمة عند النجاح. */
    private fun confirmDeleteExtension(uid: String, ext: com.dlof.rinlang.store.extensions.RinExtension) {
        AlertDialog.Builder(this)
            .setTitle(R.string.ext_delete_confirm_title)
            .setMessage(getString(R.string.ext_delete_confirm_message, ext.name))
            .setPositiveButton(R.string.ext_action_delete) { _, _ ->
                if (!isOnline()) { showOfflineOverlay(); return@setPositiveButton }
                com.dlof.rinlang.store.extensions.ExtensionRepository.deleteExtension(ext.id, uid) { success, error ->
                    if (success) {
                        Toast.makeText(this, R.string.ext_deleted_toast, Toast.LENGTH_SHORT).show()
                        loadMyExtensions(uid)
                    } else {
                        Toast.makeText(this, error ?: getString(R.string.ext_delete_failed), Toast.LENGTH_LONG).show()
                    }
                }
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
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
