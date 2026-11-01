package com.dlof.rinlang.auth

import android.os.Bundle
import android.view.View
import android.widget.Button
import android.widget.EditText
import android.widget.ProgressBar
import android.widget.TextView
import android.widget.Toast
import com.dlof.rinlang.network.BaseConnectivityActivity
import com.dlof.rinlang.R

class VerifyCodeActivity : BaseConnectivityActivity() {

    companion object {
        const val EXTRA_UID = "extra_uid"
        const val EXTRA_EMAIL = "extra_email"
        const val EXTRA_NAME = "extra_name"
    }

    private lateinit var uid: String
    private lateinit var email: String
    private lateinit var name: String

    private lateinit var edtCode: EditText
    private lateinit var btnVerify: Button
    private lateinit var progress: ProgressBar

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_verify_code)

        uid = intent.getStringExtra(EXTRA_UID) ?: run { finish(); return }
        email = intent.getStringExtra(EXTRA_EMAIL) ?: ""
        name = intent.getStringExtra(EXTRA_NAME) ?: ""

        findViewById<TextView>(R.id.txtToolbarTitle).text = getString(R.string.verify_title)
        findViewById<View>(R.id.btnToolbarBack).setOnClickListener { finish() }
        findViewById<TextView>(R.id.txtVerifySubtitle).text =
            getString(R.string.verify_subtitle_format, email)

        edtCode = findViewById(R.id.edtCode)
        btnVerify = findViewById(R.id.btnVerify)
        progress = findViewById(R.id.progressVerify)

        btnVerify.setOnClickListener { attemptVerify() }
        findViewById<TextView>(R.id.txtResendCode).setOnClickListener { resendCode() }
    }

    private fun attemptVerify() {
        val code = edtCode.text.toString().trim()
        if (code.length != 5) {
            Toast.makeText(this, R.string.hint_verification_code, Toast.LENGTH_SHORT).show()
            return
        }
        if (!isOnline()) { showOfflineOverlay(); return }

        setLoading(true)
        AuthRepository.verifyCode(uid, code) { result ->
            setLoading(false)
            when (result) {
                is VerifyResult.Success -> {
                    Toast.makeText(this, R.string.verify_success, Toast.LENGTH_SHORT).show()
                    // TODO (المرحلة التالية): الانتقال إلى شاشة "متجر Rin" الرئيسية.
                    setResult(RESULT_OK)
                    finish()
                }
                is VerifyResult.Failure -> {
                    Toast.makeText(this, result.message, Toast.LENGTH_SHORT).show()
                }
            }
        }
    }

    private fun resendCode() {
        if (!isOnline()) { showOfflineOverlay(); return }
        setLoading(true)
        AuthRepository.sendNewCode(uid, email, name) { success, error ->
            setLoading(false)
            val message = if (success) getString(R.string.code_resent) else (error ?: "فشل الإرسال")
            Toast.makeText(this, message, Toast.LENGTH_SHORT).show()
        }
    }

    private fun setLoading(loading: Boolean) {
        progress.visibility = if (loading) View.VISIBLE else View.GONE
        btnVerify.isEnabled = !loading
    }
}
