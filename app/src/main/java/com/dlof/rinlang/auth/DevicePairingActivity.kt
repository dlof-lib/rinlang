package com.dlof.rinlang.auth

import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Color
import android.os.Bundle
import android.os.CountDownTimer
import android.view.View
import android.widget.ImageView
import android.widget.TextView
import android.widget.Toast
import com.dlof.rinlang.R
import com.dlof.rinlang.network.BaseConnectivityActivity
import com.google.firebase.database.ValueEventListener
import com.google.zxing.BarcodeFormat
import com.google.zxing.EncodeHintType
import com.google.zxing.MultiFormatWriter
import com.google.zxing.qrcode.decoder.ErrorCorrectionLevel

/**
 * ===== المرحلة 2: "تأكيد الجهاز عبر GETY" =====
 *
 * تُفتح بعد نجاح المرحلة 1 (تسجيل الدخول أو التحقق من كود البريد)، ولا تكتمل عملية
 * الدخول قبل أن يقوم المستخدم بمسح الـQR (أو إدخال الكود يدوياً) داخل تطبيق GETY
 * المنفصل. راجع [PairingRepository] لتفاصيل آلية الأمان والتخزين على Firebase.
 */
class DevicePairingActivity : BaseConnectivityActivity() {

    companion object {
        const val EXTRA_UID = "extra_uid"
        /** يُعاد True في نتيجة النشاط عند اكتمال الإقران والدخول بنجاح. */
        const val RESULT_EXTRA_PAIRED = "result_paired"
    }

    private lateinit var uid: String
    private var currentToken: String? = null
    private var pairingListener: ValueEventListener? = null
    private var countDownTimer: CountDownTimer? = null

    private lateinit var imgQr: ImageView
    private lateinit var txtToken: TextView
    private lateinit var txtStatus: TextView
    private lateinit var txtCountdown: TextView
    private lateinit var txtCancel: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_device_pairing)

        uid = intent.getStringExtra(EXTRA_UID) ?: run { finish(); return }

        findViewById<TextView>(R.id.txtToolbarTitle).text = getString(R.string.pairing_title)
        findViewById<View>(R.id.btnToolbarBack).setOnClickListener { onCancelPairing() }

        imgQr = findViewById(R.id.imgPairingQr)
        txtToken = findViewById(R.id.txtPairingToken)
        txtStatus = findViewById(R.id.txtPairingStatus)
        txtCountdown = findViewById(R.id.txtPairingCountdown)
        txtCancel = findViewById(R.id.txtPairingCancel)

        txtCancel.setOnClickListener { onCancelPairing() }
        findViewById<View>(R.id.btnPairingRegenerate).setOnClickListener { requestNewCode() }

        if (!isOnline()) { showOfflineOverlay(); return }
        requestNewCode()
    }

    /** ينشئ (أو يجدّد) طلب إقران جديداً — يُستخدم أيضاً بعد انتهاء الصلاحية أو عند الضغط على "تجديد". */
    private fun requestNewCode() {
        cleanupCurrentRequest()
        txtStatus.text = getString(R.string.pairing_generating)
        PairingRepository.createPairingRequest(uid) { request, error ->
            if (request == null) {
                Toast.makeText(this, error ?: getString(R.string.pairing_error_generic), Toast.LENGTH_SHORT).show()
                return@createPairingRequest
            }
            currentToken = request.token
            txtToken.text = request.token
            imgQr.setImageBitmap(renderQrBitmap(request.token))
            txtStatus.text = getString(R.string.pairing_waiting)
            startCountdown(request.expiresAt)
            pairingListener = PairingRepository.observe(request.token) { state -> onPairingState(state) }
        }
    }

    private fun onPairingState(state: PairingState) {
        when (state) {
            is PairingState.Waiting -> txtStatus.text = getString(R.string.pairing_waiting)
            is PairingState.Confirmed -> {
                txtStatus.text = getString(R.string.pairing_confirmed)
                countDownTimer?.cancel()
                currentToken?.let { PairingRepository.cancel(it) }
                Toast.makeText(this, R.string.pairing_confirmed, Toast.LENGTH_SHORT).show()
                setResult(RESULT_OK, Intent().putExtra(RESULT_EXTRA_PAIRED, true))
                finish()
            }
            is PairingState.Expired -> {
                txtStatus.text = getString(R.string.pairing_expired)
                countDownTimer?.cancel()
            }
        }
    }

    private fun startCountdown(expiresAt: Long) {
        countDownTimer?.cancel()
        val remaining = (expiresAt - System.currentTimeMillis()).coerceAtLeast(0L)
        countDownTimer = object : CountDownTimer(remaining, 1000L) {
            override fun onTick(millisUntilFinished: Long) {
                val totalSeconds = millisUntilFinished / 1000
                val h = totalSeconds / 3600
                val m = (totalSeconds % 3600) / 60
                val s = totalSeconds % 60
                txtCountdown.text = getString(R.string.pairing_countdown_format, h, m, s)
            }
            override fun onFinish() {
                txtCountdown.text = getString(R.string.pairing_countdown_zero)
            }
        }.start()
    }

    /** يولّد صورة QR حقيقية (نفس مكتبة zxing المستخدمة في RinArtifactBridge) تحوي [token] فقط. */
    private fun renderQrBitmap(token: String): Bitmap {
        val size = 480
        val hints = hashMapOf<EncodeHintType, Any>(
            EncodeHintType.ERROR_CORRECTION to ErrorCorrectionLevel.M,
            EncodeHintType.MARGIN to 1
        )
        val matrix = MultiFormatWriter().encode("rin-pairing:$token", BarcodeFormat.QR_CODE, size, size, hints)
        val bitmap = Bitmap.createBitmap(size, size, Bitmap.Config.RGB_565)
        for (x in 0 until size) {
            for (y in 0 until size) {
                bitmap.setPixel(x, y, if (matrix[x, y]) Color.BLACK else Color.WHITE)
            }
        }
        return bitmap
    }

    private fun onCancelPairing() {
        cleanupCurrentRequest()
        setResult(RESULT_CANCELED)
        finish()
    }

    private fun cleanupCurrentRequest() {
        countDownTimer?.cancel()
        val token = currentToken
        val listener = pairingListener
        if (token != null && listener != null) {
            PairingRepository.stopObserving(token, listener)
            PairingRepository.cancel(token)
        }
        currentToken = null
        pairingListener = null
    }

    override fun onDestroy() {
        // لا نحذف الطلب هنا إن كان لا يزال قيد الانتظار الطبيعي (مثلاً تدوير الشاشة)؛
        // يُنظَّف صراحة فقط عبر الإلغاء اليدوي أو بعد التأكيد الناجح.
        val token = currentToken
        val listener = pairingListener
        if (token != null && listener != null) {
            PairingRepository.stopObserving(token, listener)
        }
        super.onDestroy()
    }
}
