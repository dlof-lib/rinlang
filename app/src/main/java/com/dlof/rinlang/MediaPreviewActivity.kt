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

        /** نصوص عادية تُعرض في العارض (قراءة + بحث)؛ بقية الأكواد تبقى في المحرر. */
        private val TEXT_EXT = setOf("txt", "md", "markdown", "log", "csv", "tsv")
        private val BIN_EXT = setOf("bin", "dat", "rom", "o", "obj", "hex")
        fun isViewerBin(name: String) = name.substringAfterLast('.', "").lowercase() in BIN_EXT

        fun isViewerText(name: String) = name.substringAfterLast('.', "").lowercase() in TEXT_EXT
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
    private lateinit var btnSearch: View
    private lateinit var btnWrap: View
    private lateinit var textViewer: TextViewerController
    private lateinit var hexViewer: HexViewController
    private lateinit var btnMore: View
    private var curDims: IntArray? = null
    private var slideshow = false
    private val mediaPrefs by lazy { getSharedPreferences("rin_media", MODE_PRIVATE) }
    private val slideRunnable = object : Runnable {
        override fun run() {
            if (!slideshow || paths.size < 2) return
            index = (index + 1) % paths.size
            showCurrent(1)
            handler.postDelayed(this, 3000L)
        }
    }
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
        btnMore = findViewById(R.id.btnMediaMore)
        btnMore.setOnClickListener { showMore() }
        applyBackground()
        btnSearch = findViewById(R.id.btnMediaSearch)
        btnWrap = findViewById(R.id.btnMediaWrap)
        textViewer = TextViewerController(this)
        hexViewer = HexViewController(this)
        btnSearch.setOnClickListener { if (hexViewer.active) hexViewer.showSearchDialog() else textViewer.toggleFind() }
        btnWrap.setOnClickListener {
            if (hexViewer.active) { hexViewer.toggleMode(); updateHexSub() } else textViewer.toggleWrap()
        }
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
            val ime = insets.getInsets(WindowInsetsCompat.Type.ime())
            textViewer.applyInsets(bars.top, maxOf(bars.bottom, ime.bottom))
            hexViewer.applyInsets(bars.top)
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

        img.onTap = { stopSlideshow(); setChrome(!chrome) }
        img.onSwipe = { dir -> go(dir) }
        img.onDismiss = { finish() }
        img.onZoom = { pct -> showZoom(pct) }

        if (paths.isEmpty()) { txtError.visibility = View.VISIBLE; return }
        showCurrent(0)
    }

    private fun go(delta: Int) {
        stopSlideshow()
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

        textViewer.hide()
        hexViewer.hide()
        btnSearch.visibility = View.GONE
        btnWrap.visibility = View.GONE
        (btnWrap as android.widget.ImageButton).setImageResource(R.drawable.ic_media_wrap)
        // أوقف أي صورة متحركة سابقة وأخفِ زر تشغيلها قبل عرض أي نوع جديد (SVG/نص/ثنائي/فيديو...)
        animated?.stop(); animated = null
        btnPlay.visibility = View.GONE
        if (video.isPlaying) video.stopPlayback()
        if (isViewerBin(file.name)) {
            img.visibility = View.GONE
            video.visibility = View.GONE
            web.visibility = View.GONE
            btnRotate.visibility = View.GONE
            btnPlay.visibility = View.GONE
            progress.visibility = View.GONE
            val kind = hexViewer.load(file)
            if (kind == null) { fail(); return }
            hexKind = kind
            hexFile = file
            btnSearch.visibility = View.VISIBLE
            btnWrap.visibility = View.VISIBLE
            (btnWrap as android.widget.ImageButton).setImageResource(R.drawable.ic_media_bin)
            btnEdit.visibility = View.GONE
            updateHexSub()
            return
        }
        if (isViewerText(file.name)) {
            img.visibility = View.GONE
            video.visibility = View.GONE
            web.visibility = View.GONE
            btnRotate.visibility = View.GONE
            btnPlay.visibility = View.GONE
            progress.visibility = View.GONE
            val info = textViewer.load(file)
            if (info == null) { fail(); txtError.text = getString(R.string.media_binary); return }
            btnSearch.visibility = View.VISIBLE
            btnWrap.visibility = View.VISIBLE
            btnEdit.visibility = if (projectName != null && rels.getOrNull(index) != null) View.VISIBLE else View.GONE
            val parts = mutableListOf(file.extension.uppercase().ifEmpty { "TXT" }, getString(R.string.media_lines, info.lines),
                Formatter.formatFileSize(this, file.length()))
            if (info.truncated) parts += getString(R.string.media_truncated)
            txtSub.text = parts.joinToString("  \u00B7  ")
            return
        }
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
        curDims = dims
        val parts = mutableListOf<String>()
        if (paths.size > 1) parts += getString(R.string.media_counter, index + 1, paths.size)
        if (tag != null) parts += tag
        if (dims != null) parts += "${dims[0]}\u00D7${dims[1]}"
        parts += Formatter.formatFileSize(this, file.length())
        parts += DateFormat.getDateInstance(DateFormat.MEDIUM).format(Date(file.lastModified()))
        txtSub.text = parts.joinToString("  \u00B7  ")
    }

    private var hexKind = ""
    private var hexFile: File? = null

    private fun updateHexSub() {
        val f = hexFile ?: return
        val parts = mutableListOf(hexViewer.modeLabel, Formatter.formatFileSize(this, f.length()))
        if (hexKind.isNotEmpty()) parts += hexKind
        parts += "${f.length()} B"
        txtSub.text = parts.filter { it.isNotEmpty() }.joinToString("  \u00B7  ")
    }

    // ---------- قائمة "المزيد": معلومات، فتح بتطبيق آخر، نسخ المسار، عرض تلقائي، خلفية ----------

    private fun showMore() {
        val file = File(paths.getOrNull(index) ?: return)
        val pm = android.widget.PopupMenu(this, btnMore)
        val imageMode = img.visibility == View.VISIBLE
        pm.menu.add(0, 1, 0, R.string.media_info)
        pm.menu.add(0, 2, 1, R.string.media_open_with)
        pm.menu.add(0, 3, 2, R.string.media_copy_path)
        if (imageMode && paths.size > 1)
            pm.menu.add(0, 4, 3, if (slideshow) R.string.media_slideshow_stop else R.string.media_slideshow_start)
        if (imageMode) pm.menu.add(0, 5, 4, R.string.media_bg)
        pm.setOnMenuItemClickListener { m ->
            when (m.itemId) {
                1 -> showInfo(file)
                2 -> openWith(file)
                3 -> copyPath(file)
                4 -> if (slideshow) stopSlideshow() else startSlideshow()
                5 -> { mediaPrefs.edit().putInt("bg", (mediaPrefs.getInt("bg", 0) + 1) % 3).apply(); applyBackground(true) }
            }
            true
        }
        pm.show()
    }

    private fun applyBackground(toast: Boolean = false) {
        val mode = mediaPrefs.getInt("bg", 0)
        img.background = when (mode) {
            1 -> android.graphics.drawable.ColorDrawable(android.graphics.Color.WHITE)
            2 -> {
                val s = (12 * resources.displayMetrics.density).toInt()
                val bm = Bitmap.createBitmap(s * 2, s * 2, Bitmap.Config.ARGB_8888)
                val c = android.graphics.Canvas(bm)
                val p = android.graphics.Paint()
                p.color = 0xFF2A2F2C.toInt(); c.drawRect(0f, 0f, s * 2f, s * 2f, p)
                p.color = 0xFF3A403C.toInt(); c.drawRect(0f, 0f, s.toFloat(), s.toFloat(), p); c.drawRect(s.toFloat(), s.toFloat(), s * 2f, s * 2f, p)
                android.graphics.drawable.BitmapDrawable(resources, bm).apply {
                    tileModeX = android.graphics.Shader.TileMode.REPEAT
                    tileModeY = android.graphics.Shader.TileMode.REPEAT
                }
            }
            else -> null
        }
        if (toast) android.widget.Toast.makeText(this,
            when (mode) { 1 -> R.string.media_bg_white; 2 -> R.string.media_bg_checker; else -> R.string.media_bg_black },
            android.widget.Toast.LENGTH_SHORT).show()
    }

    private fun startSlideshow() {
        if (paths.size < 2) return
        slideshow = true
        window.addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        setChrome(false)
        handler.postDelayed(slideRunnable, 3000L)
    }

    private fun stopSlideshow() {
        if (!slideshow) return
        slideshow = false
        handler.removeCallbacks(slideRunnable)
        window.clearFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
    }

    private fun copyPath(file: File) {
        val text = rels.getOrNull(index) ?: file.absolutePath
        val cm = getSystemService(CLIPBOARD_SERVICE) as android.content.ClipboardManager
        cm.setPrimaryClip(android.content.ClipData.newPlainText("path", text))
        android.widget.Toast.makeText(this, R.string.media_copied, android.widget.Toast.LENGTH_SHORT).show()
    }

    private fun openWith(file: File) {
        try {
            val uri = FileProvider.getUriForFile(this, "$packageName.fileprovider", file)
            val ext = file.extension.lowercase()
            val mime = android.webkit.MimeTypeMap.getSingleton().getMimeTypeFromExtension(ext) ?: "*/*"
            startActivity(Intent.createChooser(
                Intent(Intent.ACTION_VIEW).apply {
                    setDataAndType(uri, mime)
                    addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                }, file.name))
        } catch (t: Throwable) {
            android.widget.Toast.makeText(this, "${t.message}", android.widget.Toast.LENGTH_SHORT).show()
        }
    }

    private fun showInfo(file: File) {
        val sb = StringBuilder()
        fun line(label: Int, value: String?) {
            if (!value.isNullOrBlank()) sb.append(getString(label)).append(": ").append(value).append("\n")
        }
        line(R.string.media_i_name, file.name)
        line(R.string.media_i_path, rels.getOrNull(index) ?: file.absolutePath)
        line(R.string.media_i_size, Formatter.formatFileSize(this, file.length()) + " (" + file.length() + " B)")
        line(R.string.media_i_date, DateFormat.getDateTimeInstance(DateFormat.MEDIUM, DateFormat.SHORT).format(Date(file.lastModified())))
        curDims?.let { line(R.string.media_i_dims, "${it[0]}\u00D7${it[1]}") }
        try {
            val ex = android.media.ExifInterface(file.absolutePath)
            val make = ex.getAttribute(android.media.ExifInterface.TAG_MAKE)
            val model = ex.getAttribute(android.media.ExifInterface.TAG_MODEL)
            line(R.string.media_i_camera, listOfNotNull(make, model).joinToString(" ").trim())
            line(R.string.media_i_taken, ex.getAttribute(android.media.ExifInterface.TAG_DATETIME))
            ex.getAttribute(android.media.ExifInterface.TAG_EXPOSURE_TIME)?.toDoubleOrNull()?.let { e ->
                line(R.string.media_i_exposure, if (e in 0.0001..0.9999) "1/" + Math.round(1.0 / e) + " s" else "$e s")
            }
            line(R.string.media_i_aperture, ex.getAttribute(android.media.ExifInterface.TAG_F_NUMBER)?.let { "\u0192/$it" })
            line(R.string.media_i_iso, ex.getAttribute(android.media.ExifInterface.TAG_ISO_SPEED_RATINGS))
            line(R.string.media_i_focal, ex.getAttribute(android.media.ExifInterface.TAG_FOCAL_LENGTH)?.toDoubleOrNull()?.let { "%.1f mm".format(it) })
            val ll = FloatArray(2)
            if (ex.getLatLong(ll)) line(R.string.media_i_gps, "%.5f, %.5f".format(ll[0], ll[1]))
        } catch (_: Throwable) {}
        androidx.appcompat.app.AlertDialog.Builder(this)
            .setTitle(R.string.media_info)
            .setMessage(sb.toString().trim())
            .setPositiveButton(android.R.string.ok, null)
            .show()
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

    override fun dispatchTouchEvent(ev: android.view.MotionEvent): Boolean {
        if (textViewer.dispatch(ev) || hexViewer.dispatch(ev)) return true
        return super.dispatchTouchEvent(ev)
    }

    @Suppress("DEPRECATION")
    override fun onBackPressed() {
        if (textViewer.active && textViewer.findVisible) textViewer.toggleFind() else super.onBackPressed()
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
        handler.removeCallbacks(slideRunnable)
        web.destroy()
        hexViewer.close()
        token++
        super.onDestroy()
    }
}
