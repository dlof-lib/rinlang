package com.dlof.wgom

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

/**
 * WGOM — تسجيل الدخول بنفس حساب Rin (المرحلة 3: إدارة الحساب).
 * لا يُنشئ حسابات جديدة هنا؛ الحساب يُنشأ فقط من RinStudio (المرحلة 1).
 */
class LoginActivity : AppCompatActivity() {

    private lateinit var edtEmail: EditText
    private lateinit var edtPassword: EditText
    private lateinit var btnLogin: Button
    private lateinit var progress: ProgressBar
    private lateinit var txtError: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_login)

        edtEmail = findViewById(R.id.edtEmail)
        edtPassword = findViewById(R.id.edtPassword)
        btnLogin = findViewById(R.id.btnLogin)
        progress = findViewById(R.id.progress)
        txtError = findViewById(R.id.txtError)

        btnLogin.setOnClickListener { attemptLogin() }
    }

    private fun attemptLogin() {
        val email = edtEmail.text.toString().trim()
        val password = edtPassword.text.toString()
        txtError.text = ""

        if (email.isEmpty() || password.isEmpty()) {
            txtError.text = getString(R.string.error_required_fields)
            return
        }
        if (!Patterns.EMAIL_ADDRESS.matcher(email).matches()) {
            txtError.text = getString(R.string.error_email_invalid)
            return
        }

        setLoading(true)
        AuthRestClient.signIn(email, password) { session, error ->
            setLoading(false)
            if (session == null) {
                txtError.text = error ?: getString(R.string.error_generic)
                return@signIn
            }
            SessionHolder.session = session
            SessionHolder.email = email
            startActivity(Intent(this, AccountActivity::class.java))
            finish()
        }
    }

    private fun setLoading(loading: Boolean) {
        progress.visibility = if (loading) View.VISIBLE else View.GONE
        btnLogin.isEnabled = !loading
    }
}
