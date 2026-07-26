package com.dlof.rinlang.auth

import android.content.Intent
import android.os.Bundle
import android.util.Patterns
import android.view.View
import android.widget.Button
import android.widget.EditText
import android.widget.ProgressBar
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.dlof.rinlang.R

class RegisterActivity : AppCompatActivity() {

    private lateinit var edtName: EditText
    private lateinit var edtUsername: EditText
    private lateinit var edtEmail: EditText
    private lateinit var edtPassword: EditText
    private lateinit var btnRegister: Button
    private lateinit var progress: ProgressBar

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_register)

        findViewById<TextView>(R.id.txtToolbarTitle).text = getString(R.string.register_title)
        findViewById<View>(R.id.btnToolbarBack).setOnClickListener { finish() }

        edtName = findViewById(R.id.edtName)
        edtUsername = findViewById(R.id.edtUsername)
        edtEmail = findViewById(R.id.edtEmail)
        edtPassword = findViewById(R.id.edtPassword)
        btnRegister = findViewById(R.id.btnRegister)
        progress = findViewById(R.id.progressRegister)

        btnRegister.setOnClickListener { attemptRegister() }
        findViewById<TextView>(R.id.txtGoLogin).setOnClickListener { finish() }
    }

    private fun attemptRegister() {
        val name = edtName.text.toString().trim()
        val username = edtUsername.text.toString().trim()
        val email = edtEmail.text.toString().trim()
        val password = edtPassword.text.toString()

        if (name.isEmpty() || username.isEmpty() || email.isEmpty() || password.isEmpty()) {
            Toast.makeText(this, R.string.error_required_fields, Toast.LENGTH_SHORT).show()
            return
        }
        if (!Patterns.EMAIL_ADDRESS.matcher(email).matches()) {
            Toast.makeText(this, R.string.error_email_invalid, Toast.LENGTH_SHORT).show()
            return
        }
        if (password.length < 6) {
            Toast.makeText(this, R.string.error_password_short, Toast.LENGTH_SHORT).show()
            return
        }

        setLoading(true)
        AuthRepository.checkUsernameAvailable(username) { available ->
            if (!available) {
                setLoading(false)
                Toast.makeText(this, R.string.error_username_taken, Toast.LENGTH_SHORT).show()
                return@checkUsernameAvailable
            }

            AuthRepository.register(name, username, email, password) { result ->
                setLoading(false)
                when (result) {
                    is RegisterResult.NeedsVerification -> {
                        startActivity(
                            Intent(this, VerifyCodeActivity::class.java)
                                .putExtra(VerifyCodeActivity.EXTRA_UID, result.uid)
                                .putExtra(VerifyCodeActivity.EXTRA_EMAIL, result.email)
                                .putExtra(VerifyCodeActivity.EXTRA_NAME, result.name)
                        )
                        finish()
                    }
                    is RegisterResult.Failure -> {
                        Toast.makeText(this, result.message, Toast.LENGTH_SHORT).show()
                    }
                }
            }
        }
    }

    private fun setLoading(loading: Boolean) {
        progress.visibility = if (loading) View.VISIBLE else View.GONE
        btnRegister.isEnabled = !loading
    }
}
