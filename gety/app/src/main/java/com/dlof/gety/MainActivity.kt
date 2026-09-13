package com.dlof.gety

import android.os.Build
import android.os.Bundle
import android.view.View
import android.widget.Button
import android.widget.EditText
import android.widget.ProgressBar
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import com.journeyapps.barcodescanner.ScanContract
import com.journeyapps.barcodescanner.ScanOptions
import org.json.JSONObject

/**
 * GETY — تطبيق تأكيد الجهاز (المرحلة 2 من نظام دخول RinStudio).
 *
 * المستخدم يفتح RinStudio على الجهاز الذي يريد الدخول منه، فيظهر له QR وكود نصي.
 * هنا في GETY: إما مسح الـQR بالكاميرا، أو كتابة الكود يدوياً، ثم تأكيده. بمجرد
 * التأكيد، RinStudio (الذي يستمع لنفس الطلب في الوقت الفعلي) يكمل تسجيل الدخول تلقائياً.
 */
class MainActivity : AppCompatActivity() {

    private lateinit var edtCode: EditText
    private lateinit var btnScan: Button
    private lateinit var btnConfirm: Button
    private lateinit var txtStatus: TextView
    private lateinit var progress: ProgressBar

    private val scanLauncher = registerForActivityResult(ScanContract()) { result ->
        val raw = result.contents ?: return@registerForActivityResult
        edtCode.setText(extractToken(raw))
        attemptConfirm()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        edtCode = findViewById(R.id.edtCode)
        btnScan = findViewById(R.id.btnScan)
        btnConfirm = findViewById(R.id.btnConfirm)
        txtStatus = findViewById(R.id.txtStatus)
        progress = findViewById(R.id.progress)

        btnScan.setOnClickListener { launchScanner() }
        btnConfirm.setOnClickListener { attemptConfirm() }
    }

    private fun launchScanner() {
        val options = ScanOptions().apply {
            setDesiredBarcodeFormats(ScanOptions.QR_CODE)
            setPrompt(getString(R.string.scan_prompt))
            setBeepEnabled(true)
            setOrientationLocked(true)
        }
        scanLauncher.launch(options)
    }

    /** يقبل الكود سواء جاء كنص "rin-pairing:TOKEN" من الـQR أو كنص التوكن مباشرة من الإدخال اليدوي. */
    private fun extractToken(raw: String): String {
        val prefix = "rin-pairing:"
        return if (raw.startsWith(prefix)) raw.removePrefix(prefix).trim() else raw.trim()
    }

    private fun attemptConfirm() {
        val token = edtCode.text.toString().trim().uppercase()
        if (token.isEmpty()) {
            Toast.makeText(this, R.string.error_empty_code, Toast.LENGTH_SHORT).show()
            return
        }

        setLoading(true)
        txtStatus.text = getString(R.string.status_checking)
        RtdbClient.getPairing(token) { data, error ->
            if (data == null) {
                setLoading(false)
                txtStatus.text = error ?: getString(R.string.error_code_not_found)
                return@getPairing
            }
            val confirmed = data.optBoolean("confirmed", false)
            val expiresAt = data.optLong("expiresAt", 0L)
            when {
                confirmed -> {
                    setLoading(false)
                    txtStatus.text = getString(R.string.error_already_confirmed)
                }
                System.currentTimeMillis() > expiresAt -> {
                    setLoading(false)
                    txtStatus.text = getString(R.string.error_code_expired)
                }
                else -> doConfirm(token)
            }
        }
    }

    private fun doConfirm(token: String) {
        val deviceName = "${Build.MANUFACTURER} ${Build.MODEL}".trim()
        RtdbClient.confirmPairing(token, deviceName) { success, error ->
            setLoading(false)
            if (success) {
                txtStatus.text = getString(R.string.status_confirmed)
                Toast.makeText(this, R.string.status_confirmed, Toast.LENGTH_LONG).show()
                edtCode.setText("")
            } else {
                txtStatus.text = error ?: getString(R.string.error_generic)
            }
        }
    }

    private fun setLoading(loading: Boolean) {
        progress.visibility = if (loading) View.VISIBLE else View.GONE
        btnConfirm.isEnabled = !loading
        btnScan.isEnabled = !loading
    }
}
