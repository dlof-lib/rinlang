package com.dlof.rinlang.store

import android.content.Intent
import android.os.Bundle
import android.view.View
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.dlof.rinlang.R
import com.dlof.rinlang.auth.AuthRepository
import com.dlof.rinlang.auth.LoginActivity

/** شاشة "حسابي": عرض بيانات الحساب الحالي، وتسجيل الخروج، أو التوجيه لتسجيل الدخول. */
class AccountActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_account)

        findViewById<TextView>(R.id.txtToolbarTitle).text = getString(R.string.account_title)
        findViewById<View>(R.id.btnToolbarBack).setOnClickListener { finish() }

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
            findViewById<TextView>(R.id.txtAccountName).text = profile?.name ?: "—"
            findViewById<TextView>(R.id.txtAccountUsername).text = "@${profile?.username ?: ""}"
            findViewById<TextView>(R.id.txtAccountEmail).text = profile?.email ?: ""
        }

        findViewById<View>(R.id.btnLogout).setOnClickListener {
            AuthRepository.logout()
            refresh()
        }
    }
}
