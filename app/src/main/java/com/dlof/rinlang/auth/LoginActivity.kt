package com.dlof.rinlang.auth

import android.content.Intent
import android.os.Bundle
import android.view.View
import android.widget.Button
import android.widget.EditText
import android.widget.ProgressBar
import android.widget.TextView
import android.widget.Toast
import com.dlof.rinlang.network.BaseConnectivityActivity
import com.dlof.rinlang.R

class LoginActivity : BaseConnectivityActivity() {

    private lateinit var edtEmail: EditText
    private lateinit var edtPassword: EditText
    private lateinit var btnLogin: Button
    private lateinit var progress: ProgressBar

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_login)

        findViewById<TextView>(R.id.txtToolbarTitle).text = getString(R.string.login_title)
        findViewById<View>(R.id.btnToolbarBack).setOnClickListener { finish() }

        edtEmail = findViewById(R.id.edtEmail)
        edtPassword = findViewById(R.id.edtPassword)
        btnLogin = findViewById(R.id.btnLogin)
        progress = findViewById(R.id.progressLogin)

        btnLogin.setOnClickListener { attemptLogin() }
        findViewById<TextView>(R.id.txtGoRegister).setOnClickListener {
            startActivity(Intent(this, RegisterActivity::class.java))
        }
    }

    private fun attemptLogin() {
        val email = edtEmail.text.toString().trim()
        val password = edtPassword.text.toString()

        if (email.isEmpty() || password.isEmpty()) {
            Toast.makeText(this, R.string.error_required_fields, Toast.LENGTH_SHORT).show()
            return
        }
        if (!isOnline()) { showOfflineOverlay(); return }

        setLoading(true)
        AuthRepository.login(email, password) { result ->
            setLoading(false)
            when (result) {
                is LoginResult.Success -> {
                    Toast.makeText(this, getString(R.string.verify_success), Toast.LENGTH_SHORT).show()
                    // TODO (المرحلة التالية): الانتقال إلى شاشة "متجر Rin" الرئيسية بدل إغلاق الشاشة فقط.
                    finish()
                }
                is LoginResult.NeedsVerification -> {
                    AuthRepository.sendNewCode(result.uid, result.email, result.name) { _, _ -> }
                    startActivity(
                        Intent(this, VerifyCodeActivity::class.java)
                            .putExtra(VerifyCodeActivity.EXTRA_UID, result.uid)
                            .putExtra(VerifyCodeActivity.EXTRA_EMAIL, result.email)
                            .putExtra(VerifyCodeActivity.EXTRA_NAME, result.name)
                    )
                }
                is LoginResult.Failure -> {
                    Toast.makeText(this, result.message, Toast.LENGTH_SHORT).show()
                }
            }
        }
    }

    private fun setLoading(loading: Boolean) {
        progress.visibility = if (loading) View.VISIBLE else View.GONE
        btnLogin.isEnabled = !loading
    }
}
