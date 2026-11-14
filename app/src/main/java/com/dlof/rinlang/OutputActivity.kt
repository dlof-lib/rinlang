package com.dlof.rinlang

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.graphics.drawable.GradientDrawable
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.provider.Settings
import android.text.Editable
import android.text.TextWatcher
import android.view.Choreographer
import android.view.KeyEvent
import android.view.View
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputMethodManager
import android.widget.EditText
import android.widget.ImageButton
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.core.content.ContextCompat
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * جسر صغير بين [OutputActivity] و[MainActivity] (الذي يبقى حيّاً تحتها): «اذهب إلى سطر الخطأ» في المحرر،
 * وتسليم أسئلة input() لهذه الشاشة ما دامت في المقدمة ثم إعادتها للـ terminal عند الإغلاق. تضبطها
 * MainActivity في onCreate وتفرّغها في onDestroy.
 */
object RinOutputNav {
    @Volatile var goToLine: ((Int) -> Unit)? = null
    /** يُفرغ presenter الإدخال الحالي (فتُعاد الأسئلة المعلّقة للعرض في الواجهة الجديدة). */
    @Volatile var releaseInput: (() -> Unit)? = null
    /** يعيد presenter الإدخال الأصلي (terminal المحرر). */
    @Volatile var restoreInput: (() -> Unit)? = null
}

/**
 * شاشة Output المستقلة لتشغيل Rin.
 *
 * - **كتابة حرفية حيّة**: [RinOutputTypewriter] يكتب كل سطر حرفاً حرفاً بمؤشر ▌، ويتسارع تلقائياً
 *   عند كثرة المتراكم، ويدعم التخطي (زر/لمسة) وأربع سرعات، ويحترم «إزالة الحركة» في النظام.
 * - **بث حي حقيقي**: يقرأ [RinJob.liveLines] أثناء التشغيل (نفس مصدر بطاقات Runs) ثم يكمل من
 *   [RinJob.output] عند الانتهاء، بلا أي نسخة ثانية من المنطق.
 * - **ترمنال ناضج**: هامش أرقام أسطر، شريط ملوّن لكل حاوية، فلاتر خطأ/تحذير/معلومة بعدّاد حقيقي،
 *   بحث مع تظليل، ملخّص ومؤقّت حيّان، نسخ/مشاركة، إيقاف/إعادة تشغيل، وسطر إدخال لدوال input().
 */
class OutputActivity : RinBaseActivity() {

    companion object {
        const val EXTRA_JOB = "extra_job_number"
        private const val STATE_JOB = "state_job"
        private const val PREFS = "rin_output_prefs"
        private const val KEY_SPEED = "type_speed"
        private const val TICK_MS = 250L

        fun intent(context: Context, jobNumber: Int = -1): Intent =
            Intent(context, OutputActivity::class.java).apply {
                if (jobNumber > 0) putExtra(EXTRA_JOB, jobNumber)
            }
    }

    private val writer = RinOutputTypewriter()
    private lateinit var adapter: OutputRowAdapter
    private val handler = Handler(Looper.getMainLooper())
    private val choreographer: Choreographer by lazy { Choreographer.getInstance() }

    private lateinit var rv: RecyclerView
    private lateinit var txtRun: TextView
    private lateinit var txtStatus: TextView
    private lateinit var txtSummary: TextView
    private lateinit var txtTimer: TextView
    private lateinit var txtEmpty: TextView
    private lateinit var progress: View
    private lateinit var btnPrev: TextView
    private lateinit var btnNext: TextView
    private lateinit var btnSpeed: TextView
    private lateinit var btnFollow: ImageButton
    private lateinit var btnAction: ImageButton
    private lateinit var edtSearch: EditText
    private lateinit var llInput: View
    private lateinit var txtInputPrompt: TextView
    private lateinit var edtInput: EditText
    private lateinit var chipsRow: LinearLayout

    private val chipViews = ArrayList<TextView>()
    private var filterIndex = 0
    private var query = ""

    private var jobNumber = -1
    private var followLatest = true
    private var followTail = true
    private var lastLive: RinLogLine? = null
    private var finalized = false
    private var recreated = false

    private var previousHook: ((List<RinJob>) -> Unit)? = null
    private val jobsHook: (List<RinJob>) -> Unit = { jobs ->
        previousHook?.invoke(jobs)
        onJobsUpdate(jobs)
    }

    // ---- حلقة الإطارات (الكتابة) ----
    private var frameScheduled = false
    private var lastFrameNs = 0L
    private val frameCallback = Choreographer.FrameCallback { now ->
        frameScheduled = false
        val dt = if (lastFrameNs == 0L) 16L else (now - lastFrameNs) / 1_000_000L
        lastFrameNs = now
        val change = writer.step(dt)
        if (change != null) applyChange(change)
        if (writer.isTyping) {
            scheduleFrame()
        } else {
            lastFrameNs = 0L
            applyReveal()
            refreshActionState()
        }
    }

    // ---- نبض المؤشر + المؤقّت ----
    private var tickCount = 0
    private var blinkOn = true
    private val ticker = object : Runnable {
        override fun run() {
            tickCount++
            if (tickCount % 2 == 0) {
                blinkOn = !blinkOn
                adapter.tickBlink(blinkOn || writer.isTyping, (tickCount / 2) % 4)
            }
            currentJob()?.let { refreshHeader(it, onlyTimer = true) }
            handler.postDelayed(this, TICK_MS)
        }
    }

    // ---- إدخال البرنامج ----
    private var pendingDeliver: ((String?) -> Unit)? = null
    private val inputPresenter: RinInputPresenter = { prompt, deliver -> showInput(prompt, deliver) }

    // ================================================================== دورة الحياة

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_output)
        recreated = savedInstanceState != null

        rv = findViewById(R.id.rvOutput)
        txtRun = findViewById(R.id.txtOutRun)
        txtStatus = findViewById(R.id.txtOutStatus)
        txtSummary = findViewById(R.id.txtOutSummary)
        txtTimer = findViewById(R.id.txtOutTimer)
        txtEmpty = findViewById(R.id.txtOutEmpty)
        progress = findViewById(R.id.progressOut)
        btnPrev = findViewById(R.id.btnOutPrev)
        btnNext = findViewById(R.id.btnOutNext)
        btnSpeed = findViewById(R.id.btnOutSpeed)
        btnFollow = findViewById(R.id.btnOutFollow)
        btnAction = findViewById(R.id.btnOutAction)
        edtSearch = findViewById(R.id.edtOutSearch)
        llInput = findViewById(R.id.llOutInput)
        txtInputPrompt = findViewById(R.id.txtOutInputPrompt)
        edtInput = findViewById(R.id.edtOutInput)
        chipsRow = findViewById(R.id.llOutChips)

        val prefs = getSharedPreferences(PREFS, MODE_PRIVATE)
        writer.speed = if (animationsDisabled()) TypeSpeed.INSTANT
        else TypeSpeed.fromName(prefs.getString(KEY_SPEED, null))
        refreshSpeedChip()

        adapter = OutputRowAdapter(this, writer)
        adapter.onRowClick = { idx -> onRowClicked(idx) }
        adapter.onRowLongClick = { idx -> copyRow(idx) }
        rv.layoutManager = LinearLayoutManager(this)
        rv.adapter = adapter
        rv.itemAnimator = null
        rv.addOnScrollListener(object : RecyclerView.OnScrollListener() {
            override fun onScrolled(recyclerView: RecyclerView, dx: Int, dy: Int) {
                if (dy < 0) followTail = false
                else if (!recyclerView.canScrollVertically(1)) followTail = true
                btnFollow.visibility = if (followTail) View.GONE else View.VISIBLE
            }
        })

        buildChips()
        wireButtons(prefs)

        // أسئلة input(): نأخذ الـ presenter من MainActivity ما دمنا في المقدمة.
        RinOutputNav.releaseInput?.invoke()
        RinInputBridge.setPresenter(inputPresenter)

        previousHook = RinJobScheduler.onJobsChanged
        RinJobScheduler.onJobsChanged = jobsHook

        val requested = savedInstanceState?.getInt(STATE_JOB, -1)?.takeIf { it > 0 }
            ?: intent.getIntExtra(EXTRA_JOB, -1)
        openRequested(requested, restoring = recreated)
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        openRequested(intent.getIntExtra(EXTRA_JOB, -1), restoring = false)
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        outState.putInt(STATE_JOB, jobNumber)
    }

    override fun onStart() {
        super.onStart()
        handler.removeCallbacks(ticker)
        handler.postDelayed(ticker, TICK_MS)
        if (writer.isTyping) scheduleFrame()
    }

    override fun onStop() {
        handler.removeCallbacks(ticker)
        if (frameScheduled) {
            choreographer.removeFrameCallback(frameCallback)
            frameScheduled = false
        }
        lastFrameNs = 0L
        super.onStop()
    }

    override fun onDestroy() {
        if (RinJobScheduler.onJobsChanged === jobsHook) RinJobScheduler.onJobsChanged = previousHook
        previousHook = null
        // السؤال المعلّق (إن وُجد) يبقى في RinInputBridge وتعرضه واجهة المحرر بعد إعادة ضبط presenter.
        RinInputBridge.clearPresenter(inputPresenter)
        RinOutputNav.restoreInput?.invoke()
        handler.removeCallbacksAndMessages(null)
        super.onDestroy()
    }

    // ================================================================== ربط الأزرار

    private fun wireButtons(prefs: android.content.SharedPreferences) {
        findViewById<View>(R.id.btnOutBack).setOnClickListener { finish() }

        btnPrev.setOnClickListener { navigate(-1) }
        btnNext.setOnClickListener { navigate(+1) }

        btnSpeed.setOnClickListener {
            writer.speed = writer.speed.next()
            prefs.edit().putString(KEY_SPEED, writer.speed.name).apply()
            refreshSpeedChip()
            if (writer.speed == TypeSpeed.INSTANT) skipTyping()
        }

        findViewById<View>(R.id.btnOutSkip).setOnClickListener { skipTyping() }

        btnFollow.setOnClickListener {
            followTail = true
            btnFollow.visibility = View.GONE
            scrollToEnd()
        }

        findViewById<View>(R.id.btnOutCopy).setOnClickListener {
            copyToClipboard(outputText())
        }
        findViewById<View>(R.id.btnOutShare).setOnClickListener {
            val text = outputText()
            if (text.isBlank()) return@setOnClickListener
            val send = Intent(Intent.ACTION_SEND).apply {
                type = "text/plain"
                putExtra(Intent.EXTRA_SUBJECT, "Rin Run #$jobNumber")
                putExtra(Intent.EXTRA_TEXT, text)
            }
            startActivity(Intent.createChooser(send, getString(R.string.job_share_cta)))
        }

        btnAction.setOnClickListener {
            val job = currentJob() ?: return@setOnClickListener
            if (isActive(job)) {
                RinJobScheduler.stopAll()
            } else {
                followLatest = true
                if (RinJobScheduler.submit(job.source) == null) {
                    Toast.makeText(this, getString(R.string.job_queue_full_toast), Toast.LENGTH_SHORT).show()
                }
            }
        }

        findViewById<View>(R.id.btnOutSearch).setOnClickListener {
            val show = edtSearch.visibility != View.VISIBLE
            edtSearch.visibility = if (show) View.VISIBLE else View.GONE
            if (show) {
                edtSearch.requestFocus()
                (getSystemService(INPUT_METHOD_SERVICE) as? InputMethodManager)
                    ?.showSoftInput(edtSearch, InputMethodManager.SHOW_IMPLICIT)
            } else {
                edtSearch.setText("")
                (getSystemService(INPUT_METHOD_SERVICE) as? InputMethodManager)
                    ?.hideSoftInputFromWindow(edtSearch.windowToken, 0)
            }
        }
        edtSearch.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
            override fun afterTextChanged(s: Editable?) {
                query = s?.toString().orEmpty()
                applyFilter()
            }
        })

        findViewById<View>(R.id.btnOutInputSend).setOnClickListener { submitInput(cancel = false) }
        findViewById<View>(R.id.btnOutInputCancel).setOnClickListener { submitInput(cancel = true) }
        edtInput.setOnEditorActionListener { _, actionId, event ->
            val enter = event != null && event.keyCode == KeyEvent.KEYCODE_ENTER && event.action == KeyEvent.ACTION_DOWN
            if (actionId == EditorInfo.IME_ACTION_SEND || enter) { submitInput(cancel = false); true } else false
        }
    }

    // ================================================================== فتح/تبديل التشغيل

    private fun openRequested(requested: Int, restoring: Boolean) {
        val jobs = RinJobScheduler.snapshot()
        val job = jobs.firstOrNull { it.number == requested } ?: jobs.lastOrNull()
        if (job == null) {
            jobNumber = -1
            showEmpty(true)
            return
        }
        followLatest = job.number == jobs.last().number
        loadJob(job, restoring)
        updateNavButtons(jobs)
    }

    private fun loadJob(job: RinJob, restoring: Boolean) {
        showEmpty(false)
        jobNumber = job.number
        writer.reset()
        adapter.reset()
        lastLive = null
        finalized = false
        followTail = true
        btnFollow.visibility = View.GONE
        syncJob(job, first = true, instantAll = restoring)
        refreshHeader(job)
        updateSummary()
        kick()
    }

    private fun showEmpty(empty: Boolean) {
        txtEmpty.visibility = if (empty) View.VISIBLE else View.GONE
        if (empty) {
            txtRun.text = ""
            txtStatus.visibility = View.INVISIBLE
            txtTimer.text = ""
            progress.visibility = View.INVISIBLE
            btnPrev.alpha = 0.3f
            btnNext.alpha = 0.3f
            updateSummary()
            refreshActionState()
        }
    }

    private fun navigate(delta: Int) {
        val jobs = RinJobScheduler.snapshot()
        val idx = jobs.indexOfFirst { it.number == jobNumber }
        val target = idx + delta
        if (idx < 0 || target !in jobs.indices) return
        followLatest = target == jobs.lastIndex
        loadJob(jobs[target], restoring = false)
        updateNavButtons(jobs)
    }

    private fun updateNavButtons(jobs: List<RinJob>) {
        val idx = jobs.indexOfFirst { it.number == jobNumber }
        btnPrev.alpha = if (idx > 0) 1f else 0.3f
        btnNext.alpha = if (idx in 0 until jobs.lastIndex) 1f else 0.3f
    }

    private fun currentJob(): RinJob? = RinJobScheduler.snapshot().firstOrNull { it.number == jobNumber }

    private fun isActive(job: RinJob) = job.status == JobStatus.QUEUED || job.status == JobStatus.RUNNING

    // ================================================================== مزامنة الناتج من المهمة

    /** يستدعيه المجدول (على الترد الرئيسي) عند كل تغيّر: أسطر جديدة، تغيّر حالة، تشغيل جديد. */
    private fun onJobsUpdate(jobs: List<RinJob>) {
        if (isFinishing || isDestroyed) return
        val latest = jobs.lastOrNull()
        if (latest == null) {
            if (jobNumber > 0) { jobNumber = -1; writer.reset(); adapter.reset(); showEmpty(true) }
            return
        }
        if (jobNumber < 0 || (followLatest && latest.number != jobNumber)) {
            followLatest = true
            loadJob(latest, restoring = false)
            updateNavButtons(jobs)
            return
        }
        val job = jobs.firstOrNull { it.number == jobNumber }
        if (job == null) {
            // التشغيل المعروض حُذف (مسح السجل): انتقل لأحدث تشغيل متبقٍّ.
            loadJob(latest, restoring = false)
            updateNavButtons(jobs)
            return
        }
        syncJob(job, first = false, instantAll = false)
        refreshHeader(job)
        updateNavButtons(jobs)
        kick()
    }

    /**
     * يجلب من [job] ما لم يُضَف بعد إلى [writer]:
     *  - أثناء RUNNING: أسطر [RinJob.liveLines] الجديدة (التقاطع بالمرجع لا بالفهرس، لأن القائمة قد
     *    تُقصّ من البداية عند تجاوز سقفها).
     *  - عند الانتهاء (مرة واحدة): بقية أسطر [RinJob.output] ثم سطر الحالة.
     * [first] = أول تحميل لهذا التشغيل: ما قبل آخر [RinOutputTypewriter.ANIMATED_TAIL] سطر يظهر فوراً.
     */
    private fun syncJob(job: RinJob, first: Boolean, instantAll: Boolean) {
        var appended = false
        when (job.status) {
            JobStatus.QUEUED -> Unit
            JobStatus.RUNNING -> {
                val live = job.liveLines
                if (live.isNotEmpty()) {
                    val last = lastLive
                    val start = if (last == null) 0 else {
                        val i = live.indexOfLast { it === last }
                        if (i < 0) 0 else i + 1
                    }
                    if (start < live.size) {
                        val fresh = live.subList(start, live.size).toList()
                        writer.appendLines(fresh, instantCountFor(fresh.size, first, instantAll))
                        lastLive = live.last()
                        appended = true
                    }
                }
            }
            else -> if (!finalized) {
                finalized = true
                finalizeJob(job, first, instantAll)
                appended = true
            }
        }
        if (appended) {
            applyReveal()
            updateSummary()
        }
    }

    private fun instantCountFor(size: Int, first: Boolean, instantAll: Boolean): Int = when {
        instantAll -> size
        first -> maxOf(0, size - RinOutputTypewriter.ANIMATED_TAIL)
        else -> 0
    }

    private fun finalizeJob(job: RinJob, first: Boolean, instantAll: Boolean) {
        val finalLines = RinConsoleFormatter.formatLines(job.output)
        val fresh = when (job.status) {
            JobStatus.SUCCESS, JobStatus.ERROR ->
                if (finalLines.size > writer.lineCount) finalLines.drop(writer.lineCount) else emptyList()
            // المهلة/الإلغاء يستبدلان الناتج برسالة واحدة: لا تُكرَّر إن سبقها بث حي.
            else -> if (writer.lineCount == 0) finalLines else emptyList()
        }
        if (fresh.isNotEmpty()) {
            writer.appendLines(fresh, instantCountFor(fresh.size, first, instantAll))
        }
        if (writer.lineCount == 0 && job.status == JobStatus.SUCCESS) {
            writer.appendSynthetic(
                OutRowKind.STATUS, RinLogLine(LogKind.PLAIN, getString(R.string.output_no_output)), instant = instantAll
            )
        }
        val ms = job.durationMs()
        val (kind, text) = when (job.status) {
            JobStatus.SUCCESS -> LogKind.SUCCESS to getString(R.string.output_status_ok_fmt, ms)
            JobStatus.ERROR -> LogKind.ERROR to getString(R.string.output_status_error_fmt, ms)
            JobStatus.TIMEOUT -> LogKind.WARNING to getString(R.string.output_status_timeout)
            else -> LogKind.PLAIN to getString(R.string.output_status_cancelled)
        }
        writer.appendSynthetic(OutRowKind.STATUS, RinLogLine(kind, text), instant = instantAll)
    }

    // ================================================================== عرض/كتابة

    private fun scheduleFrame() {
        if (frameScheduled) return
        frameScheduled = true
        choreographer.postFrameCallback(frameCallback)
    }

    /** يكشف الصفوف الجديدة ويشغّل حلقة الكتابة إن لزم. */
    private fun kick() {
        applyReveal()
        if (writer.isTyping) scheduleFrame()
        refreshActionState()
    }

    private fun applyReveal() {
        val limit = if (writer.isTyping) writer.cursorRow + 1 else writer.rows.size
        adapter.reveal(limit)
        updateFooter()
        if (followTail) scrollToEnd()
    }

    private fun applyChange(change: RinOutputTypewriter.Change) {
        adapter.reveal(if (writer.isTyping) writer.cursorRow + 1 else writer.rows.size)
        val end = minOf(change.to, adapter.revealedCount)
        for (i in change.from until end) adapter.changedRow(i)
        updateFooter()
        if (followTail) scrollToEnd()
    }

    private fun skipTyping() {
        writer.skipAll()?.let { applyChange(it) }
        refreshActionState()
    }

    private fun scrollToEnd() {
        rv.post { rv.scrollToPosition(maxOf(adapter.itemCount - 1, 0)) }
    }

    private fun updateFooter() {
        val job = currentJob()
        adapter.setFooter(
            when {
                writer.isTyping || job == null -> OutFooterMode.HIDDEN
                pendingDeliver != null -> OutFooterMode.HIDDEN
                job.status == JobStatus.QUEUED -> OutFooterMode.QUEUED
                job.status == JobStatus.RUNNING -> OutFooterMode.RUNNING
                else -> OutFooterMode.PROMPT
            }
        )
    }

    // ================================================================== الرأس والملخّص

    private fun refreshHeader(job: RinJob, onlyTimer: Boolean = false) {
        txtTimer.text = formatDuration(job.durationMs()).takeIf { job.startedAt != 0L }.orEmpty()
        if (onlyTimer) return

        txtRun.text = getString(R.string.output_run_fmt, job.number) + "  ·  " +
            SimpleDateFormat("HH:mm:ss", Locale.getDefault()).format(Date(job.queuedAt))

        val (labelRes, colorRes) = when (job.status) {
            JobStatus.QUEUED -> R.string.job_status_queued to R.color.status_queued
            JobStatus.RUNNING -> R.string.job_status_running to R.color.status_running
            JobStatus.SUCCESS -> R.string.job_status_success to R.color.status_success
            JobStatus.ERROR -> R.string.job_status_error to R.color.status_error
            JobStatus.TIMEOUT -> R.string.job_status_timeout to R.color.status_timeout
            JobStatus.CANCELLED -> R.string.job_status_cancelled to R.color.status_cancelled
        }
        val color = ContextCompat.getColor(this, colorRes)
        txtStatus.visibility = View.VISIBLE
        txtStatus.text = getString(labelRes)
        txtStatus.setTextColor(color)
        txtStatus.background = GradientDrawable().apply {
            cornerRadius = 10f * resources.displayMetrics.density
            setColor((color and 0x00FFFFFF) or (0x2A shl 24))
        }
        progress.visibility = if (isActive(job)) View.VISIBLE else View.INVISIBLE
        refreshActionState()
    }

    private fun refreshActionState() {
        val job = currentJob()
        val active = job != null && isActive(job)
        btnAction.setImageResource(if (active) R.drawable.ic_out_stop else R.drawable.ic_out_rerun)
        btnAction.imageTintList = android.content.res.ColorStateList.valueOf(
            ContextCompat.getColor(this, if (active) R.color.status_error else R.color.rin_accent)
        )
        btnAction.contentDescription = getString(if (active) R.string.output_stop else R.string.job_rerun_cta)
        btnAction.isEnabled = job != null
        btnAction.alpha = if (job != null) 1f else 0.35f
    }

    private fun formatDuration(ms: Long): String =
        if (ms < 1000) "$ms ms" else String.format(Locale.ROOT, "%.1f s", ms / 1000.0)

    private fun outputLinesOnly(): List<RinLogLine> =
        writer.rows.filter { it.kind == OutRowKind.LINE || it.kind == OutRowKind.IMAGE }.map { it.line }

    private fun updateSummary() {
        val s = RinOutputSummary.of(outputLinesOnly())
        txtSummary.text = getString(R.string.output_summary_fmt, s.totalLines, s.errors, s.warnings)
        val counts = intArrayOf(s.totalLines, s.infos + s.debugs, s.warnings, s.errors)
        val labels = intArrayOf(R.string.job_filter_all, R.string.job_filter_info, R.string.job_filter_warning, R.string.job_filter_error)
        chipViews.forEachIndexed { i, chip ->
            chip.text = getString(labels[i]) + if (i == 0) "" else "  ${counts[i]}"
            chip.visibility = if (i == 0 || counts[i] > 0 || i == filterIndex) View.VISIBLE else View.GONE
        }
    }

    // ================================================================== الفلاتر والسرعة

    private fun buildChips() {
        val dp = resources.displayMetrics.density
        chipViews.clear()
        chipsRow.removeAllViews()
        for (i in 0 until 4) {
            val chip = TextView(this).apply {
                textSize = 11.5f
                setTypeface(typeface, android.graphics.Typeface.BOLD)
                setPadding((12 * dp).toInt(), (6 * dp).toInt(), (12 * dp).toInt(), (6 * dp).toInt())
                layoutParams = LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT
                ).apply { marginEnd = (6 * dp).toInt() }
                isClickable = true
                isFocusable = true
                setOnClickListener {
                    filterIndex = i
                    styleChips()
                    applyFilter()
                }
            }
            chipViews.add(chip)
            chipsRow.addView(chip)
        }
        styleChips()
    }

    private fun styleChips() {
        chipViews.forEachIndexed { i, chip ->
            val on = i == filterIndex
            chip.setBackgroundResource(if (on) R.drawable.bg_out_chip_on else R.drawable.bg_out_chip)
            chip.setTextColor(
                ContextCompat.getColor(
                    this,
                    when {
                        on -> R.color.rin_accent
                        i == 3 -> R.color.log_kind_error
                        i == 2 -> R.color.log_kind_warning
                        i == 1 -> R.color.log_kind_info
                        else -> R.color.out_text
                    }
                )
            )
        }
    }

    private fun applyFilter() {
        val kinds: Set<LogKind>? = when (filterIndex) {
            1 -> setOf(LogKind.INFO, LogKind.DEBUG)
            2 -> setOf(LogKind.WARNING)
            3 -> setOf(LogKind.ERROR)
            else -> null
        }
        adapter.setFilter(kinds, query)
        if (followTail) scrollToEnd()
    }

    private fun refreshSpeedChip() {
        val label = getString(
            when (writer.speed) {
                TypeSpeed.SLOW -> R.string.output_speed_slow
                TypeSpeed.NORMAL -> R.string.output_speed_normal
                TypeSpeed.FAST -> R.string.output_speed_fast
                TypeSpeed.INSTANT -> R.string.output_speed_instant
            }
        )
        btnSpeed.text = getString(R.string.output_speed_fmt, label)
    }

    /** احترام إعداد النظام «إزالة الرسوم المتحركة» (مقياس مدة المحركات = 0). */
    private fun animationsDisabled(): Boolean = try {
        Settings.Global.getFloat(contentResolver, Settings.Global.ANIMATOR_DURATION_SCALE, 1f) == 0f
    } catch (t: Throwable) {
        false
    }

    // ================================================================== تفاعل الصفوف

    private fun onRowClicked(rowIndex: Int) {
        val row = writer.rows.getOrNull(rowIndex) ?: return
        val errLine = row.line.errorLine
        val go = RinOutputNav.goToLine
        if (errLine != null && go != null && row.isDone) {
            go(errLine)
            finish()
        } else if (writer.isTyping) {
            skipTyping()
        }
    }

    private fun copyRow(rowIndex: Int) {
        val row = writer.rows.getOrNull(rowIndex) ?: return
        copyToClipboard(row.text, toast = R.string.output_line_copied)
    }

    private fun outputText(): String = RinConsoleExport.toPlainText(outputLinesOnly())

    private fun copyToClipboard(text: String, toast: Int = R.string.job_copied_toast) {
        if (text.isBlank()) return
        val cm = getSystemService(Context.CLIPBOARD_SERVICE) as? ClipboardManager ?: return
        cm.setPrimaryClip(ClipData.newPlainText("Rin Run #$jobNumber", text))
        Toast.makeText(this, getString(toast), Toast.LENGTH_SHORT).show()
    }

    // ================================================================== إدخال البرنامج

    private fun showInput(prompt: String, deliver: (String?) -> Unit) {
        // المحرك أفرغ كل ما طبعه قبل السؤال: نُدخل ما وصل بعد (دون انتظار دفعة الـ 80ms) ثم السؤال.
        currentJob()?.let { syncJob(it, first = false, instantAll = false) }
        pendingDeliver = deliver
        val clean = prompt.trim()
        writer.appendSynthetic(OutRowKind.PROMPT, RinLogLine(LogKind.PLAIN, clean.ifEmpty { "?" }))
        kick()
        txtInputPrompt.text = clean
        txtInputPrompt.visibility = if (clean.isEmpty()) View.GONE else View.VISIBLE
        edtInput.setText("")
        llInput.visibility = View.VISIBLE
        edtInput.requestFocus()
        (getSystemService(INPUT_METHOD_SERVICE) as? InputMethodManager)
            ?.showSoftInput(edtInput, InputMethodManager.SHOW_IMPLICIT)
        followTail = true
        scrollToEnd()
    }

    private fun submitInput(cancel: Boolean) {
        val deliver = pendingDeliver ?: return
        pendingDeliver = null
        val answer = if (cancel) null else edtInput.text.toString()
        llInput.visibility = View.GONE
        (getSystemService(INPUT_METHOD_SERVICE) as? InputMethodManager)
            ?.hideSoftInputFromWindow(edtInput.windowToken, 0)
        writer.appendSynthetic(
            OutRowKind.ANSWER,
            RinLogLine(LogKind.PLAIN, answer ?: "^C"),
            instant = true
        )
        kick()
        deliver(answer)
    }
}
