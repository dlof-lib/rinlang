package com.dlof.rinlang

import android.content.Intent
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Matrix
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.text.format.Formatter
import android.view.View
import android.widget.MediaController
import android.widget.ProgressBar
import android.widget.TextView
import android.widget.VideoView
import androidx.core.content.FileProvider
import androidx.core.view.ViewCompat
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.core.view.updatePadding
import java.io.File
import java.text.DateFormat
import java.util.Date

/**
 * عارض وسائط غامر داخل التطبيق: صورة (تكبير/تحريك/تدوير/تنقّل بين صور المجلد/مشاركة) أو فيديو.
 * تُفتَح من [FilesActivity]؛ عند تمرير [EXTRA_SIBLINGS] يمكن السحب أفقياً أو استخدام الأسهم للتنقل.
 * فك الترميز مُصغَّر (inSampleSize) على خيط خلفي مع احترام اتجاه EXIF، فلا خطر نفاد الذاكرة مع صور ضخمة.
 */
class MediaPreviewActivity : RinBaseActivity() {

    companion object {
        const val EXTRA_FILE_PATH = "extra_file_path"
        const val EXTRA_FILE_NAME = "extra_file_name"
        const val EXTRA_SIBLINGS = "extra_siblings"
        const val EXTRA_INDEX = "extra_index"
        /** لفتح ملف SVG في المحرر النصّي من العارض. */
        const val EXTRA_PROJECT = "extra_project"
        const val EXTRA_REL_PATHS = "extra_rel_paths"

        fun isSvg(name: String) = name.endsWith(".svg", ignoreCase = true)
    }

    private lateinit var root: View
    private lateinit var img: ZoomImageView
    private lateinit var video: VideoView
    private lateinit var txtError: TextView
    private lateinit var txtTitle: TextView
    private lateinit var txtSub: TextView
    private lateinit var txtZoom: TextView
    private lateinit var progress: ProgressBar
    private lateinit var top: View
    private lateinit var btnPrev: View
    private lateinit var btnNext: View
    private lateinit var btnRotate: View
    private lateinit var btnEdit: View
    private lateinit var btnPlay: android.widget.ImageButton
    private var animated: android.graphics.drawable.AnimatedImageDrawable? = null
    private var wasRunning = false
    private lateinit var web: android.webkit.WebView
    private var rels: List<String> = emptyList()
    private var projectName: String? = null

    private var paths: List<String> = emptyList()
    private var index = 0
    private var chrome = true
    private var token = 0
    private val handler = Handler(Looper.getMainLooper())
    private val hideZoom = Runnable {
        txtZoom.animate().alpha(0f).setDuration(200L).withEndAction { txtZoom.visibility = View.GONE }.start()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_media_preview)

        root = findViewById(R.id.mediaRoot)
        img = findViewById(R.id.imgMediaPreview)
        video = findViewById(R.id.videoMediaPreview)
        txtError = findViewById(R.id.txtMediaPreviewError)
        txtTitle = findViewById(R.id.txtMediaTitle)
        txtSub = findViewById(R.id.txtMediaSub)
        txtZoom = findViewById(R.id.txtMediaZoom)
        progress = findViewById(R.id.progressMedia)
        top = findViewById(R.id.layoutMediaTop)
        btnPrev = findViewById(R.id.btnMediaPrev)
        btnNext = findViewById(R.id.btnMediaNext)
        btnRotate = findViewById(R.id.btnMediaRotate)
        btnEdit = findViewById(R.id.btnMediaEdit)
        btnPlay = findViewById(R.id.btnMediaPlay)
        btnPlay.setOnClickListener { toggleAnimation() }
        web = findViewById(R.id.webMediaSvg)
        setupSvgWeb()

        val path = intent.getStringExtra(EXTRA_FILE_PATH)
        val fileName = intent.getStringExtra(EXTRA_FILE_NAME) ?: path?.let { File(it).name } ?: ""
        val sib = intent.getStringArrayExtra(EXTRA_SIBLINGS)?.toList().orEmpty()
        val relArr = intent.getStringArrayExtra(EXTRA_REL_PATHS)?.toList().orEmpty()
        projectName = intent.getStringExtra(EXTRA_PROJECT)
        if (sib.size > 1 && (ProjectManager.isImageFile(fileName) || isSvg(fileName))) {
            paths = sib
            rels = relArr
            index = intent.getIntExtra(EXTRA_INDEX, 0).coerceIn(0, sib.lastIndex)
        } else {
            paths = listOfNotNull(path)
            rels = relArr.take(1)
        }

        // الشريط العلوي تحت شريط الحالة، وأيقونات النظام فاتحة فوق الخلفية السوداء.
        val topPad = top.paddingTop
        ViewCompat.setOnApplyWindowInsetsListener(root) { _, insets ->
            val bars = insets.getInsets(WindowInsetsCompat.Type.systemBars())
            top.updatePadding(top = topPad + bars.top)
            insets
        }
        WindowCompat.getInsetsController(window, window.decorView).apply {
            isAppearanceLightStatusBars = false
            isAppearanceLightNavigationBars = false
            systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }

        findViewById<View>(R.id.btnMediaBack).setOnClickListener { finish() }
        findViewById<View>(R.id.btnMediaShare).setOnClickListener { share() }
        btnRotate.setOnClickListener { img.rotate90() }
        btnEdit.setOnClickListener { editCurrent() }
        btnPrev.setOnClickListener { go(-1) }
        btnNext.setOnClickListener { go(1) }

        img.onTap = { setChrome(!chrome) }
        img.onSwipe = { dir -> go(dir) }
        img.onDismiss = { finish() }
        img.onZoom = { pct -> showZoom(pct) }

        if (paths.isEmpty()) { txtError.visibility = View.VISIBLE; return }
        showCurrent(0)
    }

    private fun go(delta: Int) {
        val n = index + delta
        if (n !in paths.indices) {
            // ارتداد خفيف عند الحافة
            img.animate().translationX(-delta * 24f).setDuration(90L)
                .withEndAction { img.animate().translationX(0f).setDuration(140L).start() }.start()
            return
        }
        index = n
        showCurrent(delta)
    }

    private fun showCurrent(dir: Int) {
        val file = File(paths[index])
        txtTitle.text = file.name
        val multi = paths.size > 1
        btnPrev.visibility = if (multi && chrome) View.VISIBLE else View.GONE
        btnNext.visibility = if (multi && chrome) View.VISIBLE else View.GONE
        txtError.visibility = View.GONE
        if (!file.exists()) { fail(); return }

        if (isSvg(file.name)) {
            img.visibility = View.GONE
            video.visibility = View.GONE
            btnRotate.visibility = View.GONE
            btnEdit.visibility = if (projectName != null && rels.getOrNull(index) != null) View.VISIBLE else View.GONE
            showSvg(file)
            return
        }
        web.visibility = View.GONE
        btnEdit.visibility = View.GONE
        animated?.stop(); animated = null
        btnPlay.visibility = View.GONE
        if (ProjectManager.isVideoFile(file.name)) {
            img.visibility = View.GONE
            btnRotate.visibility = View.GONE
            txtSub.text = Formatter.formatFileSize(this, file.length())
            playVideo(file)
            return
        }
        video.visibility = View.GONE
        btnRotate.visibility = View.VISIBLE
        img.visibility = View.VISIBLE
        progress.visibility = View.VISIBLE
        subtitle(file, null)

        val my = ++token
        val dm = resources.displayMetrics
        val target = (maxOf(dm.widthPixels, dm.heightPixels) * 2).coerceIn(1024, 4096)
        val ext = file.extension.lowercase()
        Thread {
            val animRes = if (ext == "gif" || ext == "webp") decodeAnimated(file, target) else null
            val res = if (animRes == null) decode(file, target) else null
            runOnUiThread {
                if (my != token || isFinishing) return@runOnUiThread
                progress.visibility = View.GONE
                if (animRes != null) {
                    val d = animRes.first
                    if (d is android.graphics.drawable.AnimatedImageDrawable) {
                        animated = d
                        d.repeatCount = android.graphics.drawable.AnimatedImageDrawable.REPEAT_INFINITE
                        img.setAnimated(d)
                        d.start()
                        btnPlay.setImageResource(R.drawable.ic_media_pause)
                        btnPlay.visibility = if (chrome) View.VISIBLE else View.GONE
                    } else if (d is android.graphics.drawable.BitmapDrawable) {
                        img.setBitmap(d.bitmap)
                    } else { fail(); return@runOnUiThread }
                    subtitle(file, animRes.second, ext.uppercase())
                    if (dir != 0) {
                        img.translationX = dir * 80f; img.alpha = 0f
                        img.animate().translationX(0f).alpha(1f).setDuration(160L).start()
                    } else { img.alpha = 1f; img.translationX = 0f }
                    return@runOnUiThread
                }
                if (res == null) { fail(); return@runOnUiThread }
                img.setBitmap(res.first)
                subtitle(file, res.second)
                if (dir != 0) {
                    img.translationX = dir * 80f; img.alpha = 0f
                    img.animate().translationX(0f).alpha(1f).setDuration(160L).start()
                } else { img.alpha = 1f; img.translationX = 0f }
            }
        }.start()
    }

    @android.annotation.SuppressLint("ClickableViewAccessibility")
    private fun setupSvgWeb() {
        web.setBackgroundColor(android.graphics.Color.BLACK)
        web.settings.apply {
            javaScriptEnabled = false          // SVG عبر <img> أصلاً لا ينفّذ سكربتات؛ نُغلق JS احتياطاً
            allowFileAccess = false
            allowContentAccess = false
            blockNetworkLoads = true
            setSupportZoom(true)
            builtInZoomControls = true
            displayZoomControls = false
            useWideViewPort = true
            loadWithOverviewMode = true
        }
        val tap = android.view.GestureDetector(this, object : android.view.GestureDetector.SimpleOnGestureListener() {
            override fun onSingleTapUp(e: android.view.MotionEvent): Boolean { setChrome(!chrome); return false }
        })
        web.setOnTouchListener { _, ev -> tap.onTouchEvent(ev); false }
    }

    /** يعرض SVG داخل <img> بـ data-URI (لا تنفيذ سكربتات ولا موارد خارجية) فوق رقعة شطرنج فاتحة تُظهر الشفافية. */
    private fun showSvg(file: File) {
        progress.visibility = View.GONE
        if (file.length() > 6L * 1024 * 1024) { fail(); return }
        val bytes = try { file.readBytes() } catch (t: Throwable) { fail(); return }
        val b64 = android.util.Base64.encodeToString(bytes, android.util.Base64.NO_WRAP)
        val html = "<html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1,minimum-scale=1,maximum-scale=10,user-scalable=yes\">" +
            "<style>html,body{margin:0;height:100%}body{display:flex;align-items:center;justify-content:center;" +
            "background:conic-gradient(#e9eeeb 25%,#fff 0 50%,#e9eeeb 0 75%,#fff 0) 0 0/24px 24px}" +
            "img{width:100%;height:100%;object-fit:contain}</style></head><body>" +
            "<img src=\"data:image/svg+xml;base64,$b64\"></body></html>"
        web.loadDataWithBaseURL(null, html, "text/html", "utf-8", null)
        web.visibility = View.VISIBLE
        val text = String(bytes, 0, minOf(bytes.size, 4096), Charsets.UTF_8)
        val vb = Regex("""viewBox\s*=\s*["']\s*[-\d.]+[ ,]+[-\d.]+[ ,]+([\d.]+)[ ,]+([\d.]+)""").find(text)
        val dims = vb?.let { m ->
            val w = m.groupValues[1].toFloatOrNull(); val h = m.groupValues[2].toFloatOrNull()
            if (w != null && h != null) intArrayOf(w.toInt(), h.toInt()) else null
        }
        subtitle(file, dims, "SVG")
    }

    private fun editCurrent() {
        val rel = rels.getOrNull(index) ?: return
        val pn = projectName ?: return
        startActivity(Intent(this, MainActivity::class.java).apply {
            putExtra(MainActivity.EXTRA_PROJECT_NAME, pn)
            putExtra(MainActivity.EXTRA_FILE_NAME, rel)
        })
    }

    private fun subtitle(file: File, dims: IntArray?, tag: String? = null) {
        val parts = mutableListOf<String>()
        if (paths.size > 1) parts += getString(R.string.media_counter, index + 1, paths.size)
        if (tag != null) parts += tag
        if (dims != null) parts += "${dims[0]}\u00D7${dims[1]}"
        parts += Formatter.formatFileSize(this, file.length())
        parts += DateFormat.getDateInstance(DateFormat.MEDIUM).format(Date(file.lastModified()))
        txtSub.text = parts.joinToString("  \u00B7  ")
    }

    private fun fail() {
        progress.visibility = View.GONE
        img.visibility = View.GONE
        video.visibility = View.GONE
        web.visibility = View.GONE
        btnPlay.visibility = View.GONE
        txtError.text = getString(R.string.media_decode_error)
        txtError.visibility = View.VISIBLE
    }

    /** يفك الترميز بتصغير مناسب ويطبّق اتجاه EXIF؛ يعيد (الصورة، الأبعاد الأصلية بعد الاتجاه). */
    private fun decode(file: File, target: Int): Pair<Bitmap, IntArray>? = try {
        val o = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        BitmapFactory.decodeFile(file.absolutePath, o)
        val w = o.outWidth; val h = o.outHeight
        if (w <= 0 || h <= 0) null else {
            var s = 1
            while (maxOf(w, h) / s > target) s *= 2
            val bm = BitmapFactory.decodeFile(file.absolutePath, BitmapFactory.Options().apply { inSampleSize = s })
            if (bm == null) null else {
                val orient = try {
                    android.media.ExifInterface(file.absolutePath)
                        .getAttributeInt(android.media.ExifInterface.TAG_ORIENTATION, android.media.ExifInterface.ORIENTATION_NORMAL)
                } catch (t: Throwable) { android.media.ExifInterface.ORIENTATION_NORMAL }
                val mx = Matrix()
                when (orient) {
                    android.media.ExifInterface.ORIENTATION_ROTATE_90 -> mx.postRotate(90f)
                    android.media.ExifInterface.ORIENTATION_ROTATE_180 -> mx.postRotate(180f)
                    android.media.ExifInterface.ORIENTATION_ROTATE_270 -> mx.postRotate(270f)
                    android.media.ExifInterface.ORIENTATION_FLIP_HORIZONTAL -> mx.postScale(-1f, 1f)
                    android.media.ExifInterface.ORIENTATION_FLIP_VERTICAL -> mx.postScale(1f, -1f)
                }
                val out = if (mx.isIdentity) bm else Bitmap.createBitmap(bm, 0, 0, bm.width, bm.height, mx, true)
                val swapped = orient == android.media.ExifInterface.ORIENTATION_ROTATE_90 ||
                    orient == android.media.ExifInterface.ORIENTATION_ROTATE_270
                out to (if (swapped) intArrayOf(h, w) else intArrayOf(w, h))
            }
        }
    } catch (t: Throwable) { null }

    /** يفك GIF / WebP عبر ImageDecoder (يعيد AnimatedImageDrawable إن كان متحركاً) بتصغير للصور الضخمة. */
    private fun decodeAnimated(file: File, target: Int): Pair<android.graphics.drawable.Drawable, IntArray>? = try {
        val src = android.graphics.ImageDecoder.createSource(file)
        var w = 0; var h = 0
        val d = android.graphics.ImageDecoder.decodeDrawable(src) { dec, info, _ ->
            w = info.size.width; h = info.size.height
            var s = 1
            while (maxOf(w, h) / s > target) s *= 2
            if (s > 1) dec.setTargetSampleSize(s)
        }
        d to intArrayOf(w, h)
    } catch (t: Throwable) { null }

    private fun toggleAnimation() {
        val d = animated ?: return
        if (d.isRunning) { d.stop(); btnPlay.setImageResource(R.drawable.ic_media_play) }
        else { d.start(); btnPlay.setImageResource(R.drawable.ic_media_pause) }
    }

    private fun playVideo(file: File) {
        video.visibility = View.VISIBLE
        try {
            val controller = MediaController(this)
            controller.setAnchorView(video)
            video.setMediaController(controller)
            video.setVideoURI(Uri.fromFile(file))
            video.setOnPreparedListener { it.isLooping = false }
            video.setOnErrorListener { _, _, _ -> fail(); true }
            video.start()
        } catch (t: Throwable) { fail() }
    }

    private fun share() {
        val file = File(paths.getOrNull(index) ?: return)
        try {
            val uri = FileProvider.getUriForFile(this, "$packageName.fileprovider", file)
            val type = if (ProjectManager.isVideoFile(file.name)) "video/*" else "image/*"
            startActivity(Intent.createChooser(
                Intent(Intent.ACTION_SEND).apply {
                    this.type = type
                    putExtra(Intent.EXTRA_STREAM, uri)
                    addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                }, file.name))
        } catch (t: Throwable) {
            android.widget.Toast.makeText(this, "${t.message}", android.widget.Toast.LENGTH_SHORT).show()
        }
    }

    /** إظهار/إخفاء الشريط العلوي والأسهم وأشرطة النظام بلمسة واحدة. */
    private fun setChrome(visible: Boolean) {
        chrome = visible
        val multi = paths.size > 1
        val views = listOfNotNull(top, if (multi) btnPrev else null, if (multi) btnNext else null)
        views.forEach { v ->
            if (visible) { v.visibility = View.VISIBLE; v.animate().alpha(1f).setDuration(160L).start() }
            else v.animate().alpha(0f).setDuration(160L).withEndAction { if (!chrome) v.visibility = View.GONE }.start()
        }
        WindowCompat.getInsetsController(window, window.decorView).apply {
            if (visible) show(WindowInsetsCompat.Type.systemBars()) else hide(WindowInsetsCompat.Type.systemBars())
        }
    }

    private fun showZoom(pct: Int) {
        if (pct in 98..102) return
        txtZoom.text = "$pct%"
        handler.removeCallbacks(hideZoom)
        txtZoom.visibility = View.VISIBLE
        txtZoom.animate().alpha(1f).setDuration(80L).start()
        handler.postDelayed(hideZoom, 900L)
    }

    override fun onPause() {
        super.onPause()
        if (video.visibility == View.VISIBLE && video.isPlaying) video.pause()
        wasRunning = animated?.isRunning == true
        animated?.stop()
    }

    override fun onResume() {
        super.onResume()
        if (wasRunning) animated?.start()
    }

    override fun onDestroy() {
        handler.removeCallbacks(hideZoom)
        web.destroy()
        token++
        super.onDestroy()
    }
}
