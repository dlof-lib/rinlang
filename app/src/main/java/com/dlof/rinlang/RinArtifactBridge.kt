package com.dlof.rinlang

import com.google.zxing.BarcodeFormat
import com.google.zxing.EncodeHintType
import com.google.zxing.MultiFormatWriter
import com.google.zxing.qrcode.decoder.ErrorCorrectionLevel

/** Real Android-side encoders used by Rin's native Artifact runtime. */
object RinArtifactBridge {
    @JvmStatic
    fun generateQrSvg(data: String, size: Int, margin: Int): String {
        val hints = hashMapOf<EncodeHintType, Any>(
            EncodeHintType.ERROR_CORRECTION to ErrorCorrectionLevel.M,
            EncodeHintType.MARGIN to margin.coerceIn(0, 20)
        )
        val matrix = MultiFormatWriter().encode(data, BarcodeFormat.QR_CODE, size.coerceIn(64, 4096), size.coerceIn(64, 4096), hints)
        val w = matrix.width
        val h = matrix.height
        val sb = StringBuilder(w * h * 8 + 200)
        sb.append("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"").append(w)
            .append("\" height=\"").append(h).append("\" viewBox=\"0 0 ").append(w).append(' ').append(h).append("\">")
        sb.append("<rect width=\"100%\" height=\"100%\" fill=\"white\"/>")
        sb.append("<path fill=\"black\" d=\"")
        for (y in 0 until h) {
            var x = 0
            while (x < w) {
                if (!matrix[x, y]) { x++; continue }
                val start = x
                while (x < w && matrix[x, y]) x++
                sb.append('M').append(start).append(' ').append(y)
                    .append('h').append(x - start).append("v1h-").append(x - start).append('z')
            }
        }
        sb.append("\"/>")
        sb.append("</svg>")
        return sb.toString()
    }
}
