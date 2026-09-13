package com.dlof.wgom

import android.content.Intent
import android.os.Bundle
import android.view.View
import android.widget.Button
import android.widget.EditText
import android.widget.ProgressBar
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity

/**
 * WGOM — شاشة إدارة الحساب (المرحلة 3): عرض/تعديل الاسم واسم المستخدم، وتغيير كلمة السر.
 * كل تعديل يُكتب مباشرة إلى Realtime Database / Firebase Auth الخاصين بحساب RinStudio نفسه،
 * فتظهر التغييرات فوراً عند فتح RinStudio مرة أخرى.
 */
class AccountActivity : AppCompatActivity() {

    private lateinit var txtEmail: TextView
    private lateinit var edtName: EditText
    private lateinit var edtUsername: EditText
    private lateinit var btnSaveProfile: Button
    private lateinit var progressProfile: ProgressBar

    private lateinit var edtNewPassword: EditText
    private lateinit var edtConfirmPassword: EditText
    private lateinit var btnChangePassword: Button
    private lateinit var progressPassword: ProgressBar

    private lateinit var btnLogout: Button

    private var uid: String = ""
    private var idToken: String = ""

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_account)

        val session = SessionHolder.session
        if (session == null) {
            startActivity(Intent(this, LoginActivity::class.java))
            finish()
            return
        }
        uid = session.uid
        idToken = session.idToken

        txtEmail = findViewById(R.id.txtEmail)
        edtName = findViewById(R.id.edtName)
        edtUsername = findViewById(R.id.edtUsername)
        btnSaveProfile = findViewById(R.id.btnSaveProfile)
        progressProfile = findViewById(R.id.progressProfile)

        edtNewPassword = findViewById(R.id.edtNewPassword)
        edtConfirmPassword = findViewById(R.id.edtConfirmPassword)
        btnChangePassword = findViewById(R.id.btnChangePassword)
        progressPassword = findViewById(R.id.progressPassword)

        btnLogout = findViewById(R.id.btnLogout)

        txtEmail.text = SessionHolder.email
        btnSaveProfile.setOnClickListener { saveProfile() }
        btnChangePassword.setOnClickListener { changePassword() }
        btnLogout.setOnClickListener {
            SessionHolder.clear()
            startActivity(Intent(this, LoginActivity::class.java))
            finish()
        }

        loadProfile()
    }

    private fun loadProfile() {
        RtdbRestClient.fetchProfile(uid, idToken) { profile, error ->
            if (profile == null) {
                Toast.makeText(this, error ?: getString(R.string.error_generic), Toast.LENGTH_SHORT).show()
                return@fetchProfile
            }
            edtName.setText(profile.name)
            edtUsername.setText(profile.username)
        }
    }

    private fun saveProfile() {
        val name = edtName.text.toString().trim()
        val username = edtUsername.text.toString().trim()
        if (name.isEmpty() || username.isEmpty()) {
            Toast.makeText(this, R.string.error_required_fields, Toast.LENGTH_SHORT).show()
            return
        }

        setProfileLoading(true)
        RtdbRestClient.isUsernameTaken(username, uid, idToken) { taken ->
            if (taken) {
                setProfileLoading(false)
                Toast.makeText(this, R.string.error_username_taken, Toast.LENGTH_SHORT).show()
                return@isUsernameTaken
            }
            RtdbRestClient.updateProfile(uid, idToken, name, username) { success, error ->
                setProfileLoading(false)
                Toast.makeText(
                    this,
                    if (success) getString(R.string.profile_saved) else (error ?: getString(R.string.error_generic)),
                    Toast.LENGTH_SHORT
                ).show()
            }
        }
    }

    private fun changePassword() {
        val newPassword = edtNewPassword.text.toString()
        val confirmPassword = edtConfirmPassword.text.toString()

        if (newPassword.isEmpty() || confirmPassword.isEmpty()) {
            Toast.makeText(this, R.string.error_required_fields, Toast.LENGTH_SHORT).show()
            return
        }
        if (newPassword.length < 6) {
            Toast.makeText(this, R.string.error_password_short, Toast.LENGTH_SHORT).show()
            return
        }
        if (newPassword != confirmPassword) {
            Toast.makeText(this, R.string.error_password_mismatch, Toast.LENGTH_SHORT).show()
            return
        }

        setPasswordLoading(true)
        AuthRestClient.changePassword(idToken, newPassword) { success, error ->
            setPasswordLoading(false)
            if (success) {
                edtNewPassword.setText("")
                edtConfirmPassword.setText("")
                Toast.makeText(this, R.string.password_changed, Toast.LENGTH_SHORT).show()
            } else {
                Toast.makeText(this, error ?: getString(R.string.error_generic), Toast.LENGTH_SHORT).show()
            }
        }
    }

    private fun setProfileLoading(loading: Boolean) {
        progressProfile.visibility = if (loading) View.VISIBLE else View.GONE
        btnSaveProfile.isEnabled = !loading
    }

    private fun setPasswordLoading(loading: Boolean) {
        progressPassword.visibility = if (loading) View.VISIBLE else View.GONE
        btnChangePassword.isEnabled = !loading
    }
}
