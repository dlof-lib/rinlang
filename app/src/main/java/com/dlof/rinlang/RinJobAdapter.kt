package com.dlof.rinlang

import android.Manifest
import android.app.Activity
import android.app.AlertDialog
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Typeface
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.text.Editable
import android.text.SpannableString
import android.text.Spanned
import android.text.TextWatcher
import android.text.style.BackgroundColorSpan
import android.text.style.StyleSpan
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.EditText
import android.widget.HorizontalScrollView
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import androidx.recyclerview.widget.RecyclerView
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/** Which body a job's card is currently showing (section 10: Code Output UI tabs). Persisted per
 *  job number in [RinJobAdapter], same pattern as the expand/collapse state. */
private enum class JobTab { OUTPUT, EVENTS, DIAGNOSTICS }

/** Wall-clock "queued at" stamp shown next to each job's duration. Only ever touched from the
 *  main thread (RecyclerView bind), so a single shared formatter is safe here. */
private val TIME_FORMAT = SimpleDateFormat("HH:mm:ss", Locale.getDefault())

class RinJobAdapter(private val context: Context) : RecyclerView.Adapter<RinJobAdapter.JobViewHolder>() {

    private var items: List<RinJob> = emptyList()

    /** Invoked with a job's [RinJob.number] when the user asks to cancel a still-QUEUED run. */
    var onCancelRequested: ((Int) -> Unit)? = null

    /** Invoked with a job's [RinJob.number] when the user taps the pin toggle. */
    var onPinToggleRequested: ((Int) -> Unit)? = null

    /** Invoked with a finished job's original [RinJob.source] when the user taps "Run again"
     *  (footer actions row) — re-submits that exact source as a brand-new job, same as pasting
     *  it back into the editor and pressing Run, without touching the editor's current text. */
    var onRerunRequested: ((String) -> Unit)? = null

    /** Explicit user overrides of the expand/collapse state, keyed by [RinJob.number] rather
     *  than list position -- survives reordering, job completion (QUEUED -> RUNNING -> SUCCESS)
     *  and RecyclerView view-holder recycling. Absent from this map means "use the default"
     *  (see [isExpanded]), not "collapsed". */
    private val manualExpandState = HashMap<Int, Boolean>()

    /** Selected tab per job number (section 10). Absent means OUTPUT, the default. */
    private val selectedTab = HashMap<Int, JobTab>()

    /** Selected Events-tab filter chip per job number (section 11). Null means ALL. */
    private val selectedEventFilter = HashMap<Int, RinEventType?>()

    /** Current search text per job number (section 12) -- applies to whichever tab is active. */
    private val searchQuery = HashMap<Int, String>()

    fun submit(newItems: List<RinJob>) {
        items = newItems
        notifyDataSetChanged()
    }

    /** Default: the most recently queued run (the one the person just tapped "Run" for) starts
     *  expanded so its output is visible immediately, and every still-active (QUEUED/RUNNING) run
     *  is always expanded too. Older finished runs default to collapsed, so a long run history
     *  stays scannable one-line-per-card instead of one giant wall of expanded output -- same
     *  idea as a real IDE's Run panel. A manual tap on the header always wins over this default. */
    private fun isExpanded(job: RinJob): Boolean {
        manualExpandState[job.number]?.let { return it }
        if (job.status == JobStatus.QUEUED || job.status == JobStatus.RUNNING) return true
        return items.lastOrNull()?.number == job.number
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): JobViewHolder {
        val view = LayoutInflater.from(parent.context).inflate(R.layout.item_rin_job, parent, false)
        return JobViewHolder(view)
    }

    override fun onBindViewHolder(holder: JobViewHolder, position: Int) {
        holder.bind(items[position])
    }

    override fun getItemCount(): Int = items.size

    /** Stops any running per-second duration ticker (see [JobViewHolder.startDurationTicker])
     *  before a row is recycled into a different job — otherwise a RUNNING job's ticker would
     *  keep firing against whatever unrelated job the recycled row gets bound to next. */
    override fun onViewRecycled(holder: JobViewHolder) {
        holder.stopDurationTicker()
        super.onViewRecycled(holder)
    }

    inner class JobViewHolder(itemView: View) : RecyclerView.ViewHolder(itemView) {
        private val dot: View = itemView.findViewById(R.id.viewStatusDot)
        private val title: TextView = itemView.findViewById(R.id.txtJobTitle)
        private val statusText: TextView = itemView.findViewById(R.id.txtJobStatus)
        private val time: TextView = itemView.findViewById(R.id.txtJobTime)
        private val duration: TextView = itemView.findViewById(R.id.txtJobDuration)
        private val fallbackOutput: TextView = itemView.findViewById(R.id.txtJobOutput)
        private val outputLines: LinearLayout = itemView.findViewById(R.id.llJobOutputLines)
        private val eventLines: LinearLayout = itemView.findViewById(R.id.llJobEventLines)
        private val diagnosticsTab: LinearLayout = itemView.findViewById(R.id.llJobDiagnosticsTab)
        private val artifactsContainer: LinearLayout = itemView.findViewById(R.id.llJobArtifacts)
        private val headerRow: LinearLayout = itemView.findViewById(R.id.llJobHeader)
        private val expandIcon: ImageView = itemView.findViewById(R.id.imgJobExpand)
        private val detailContainer: LinearLayout = itemView.findViewById(R.id.llJobDetail)
        private val statsDivider: View = itemView.findViewById(R.id.dividerJobStats)
        private val statsText: TextView = itemView.findViewById(R.id.txtJobStats)
        private val tabOutput: TextView = itemView.findViewById(R.id.txtTabOutput)
        private val tabEvents: TextView = itemView.findViewById(R.id.txtTabEvents)
        private val tabDiagnostics: TextView = itemView.findViewById(R.id.txtTabDiagnostics)
        private val searchBox: EditText = itemView.findViewById(R.id.edtJobSearch)
        private val eventFilterScroll: HorizontalScrollView = itemView.findViewById(R.id.scrollEventFilters)
        private val eventFilterChips: LinearLayout = itemView.findViewById(R.id.llEventFilterChips)
        private val footerActions: LinearLayout = itemView.findViewById(R.id.llJobFooterActions)
        private val footerCopy: TextView = itemView.findViewById(R.id.txtFooterCopy)
        private val footerShare: TextView = itemView.findViewById(R.id.txtFooterShare)
        private val footerRerun: TextView = itemView.findViewById(R.id.txtFooterRerun)
        private val dp = itemView.resources.displayMetrics.density

        /** المُوقِّت الحي: بينما التشغيل RUNNING، يُعيد رسم [duration] كل نصف ثانية من
         *  [RinJob.durationMs] الحقيقي، بدل الانتظار حتى وصول جزء ناتج جديد (section: مؤشّر
         *  تنفيذ حي حتى لو كان البرنامج صامتاً لثوانٍ، كحساب طويل بلا print). */
        private val tickerHandler = Handler(Looper.getMainLooper())
        private var ticker: Runnable? = null

        /** Guards [searchBox]'s TextWatcher while [bind] programmatically restores this row's
         *  stored query for a (possibly different) job after recycling -- without this, that
         *  restore would itself fire the watcher and stomp [searchQuery] for the wrong job. */
        private var suppressSearchWatcher = false

        /** Built once per recycled row and toggled visible only while the job is QUEUED. */
        private val cancelBtn: TextView = TextView(context).apply {
            text = "✕"
            textSize = 13f
            setTypeface(typeface, Typeface.BOLD)
            setTextColor(ContextCompat.getColor(context, R.color.rin_editor_hint))
            setPadding((8 * dp).toInt(), 0, (2 * dp).toInt(), 0)
            contentDescription = context.getString(R.string.job_cancel_cta)
            isClickable = true
            isFocusable = true
        }.also { (duration.parent as LinearLayout).addView(it) }

        /** Pin toggle (section 9: Pinned Runs) — kept out of history trimming while pinned. */
        private val pinBtn: TextView = TextView(context).apply {
            textSize = 13f
            setPadding((8 * dp).toInt(), 0, (2 * dp).toInt(), 0)
            isClickable = true
            isFocusable = true
        }.also { (duration.parent as LinearLayout).addView(it) }

        private var boundJob: RinJob? = null

        init {
            // Tapping the header toggles the collapsed/expanded detail body (output, artifacts,
            // stats) for this specific run -- keyed by job number in the adapter so it survives
            // recycling/rebinding instead of resetting on every scroll.
            headerRow.setOnClickListener {
                val job = boundJob ?: return@setOnClickListener
                val expandedNow = isExpanded(job)
                manualExpandState[job.number] = !expandedNow
                applyExpandState(!expandedNow)
            }

            tabOutput.setOnClickListener { boundJob?.let { selectTab(it, JobTab.OUTPUT) } }
            tabEvents.setOnClickListener { boundJob?.let { selectTab(it, JobTab.EVENTS) } }
            tabDiagnostics.setOnClickListener { boundJob?.let { selectTab(it, JobTab.DIAGNOSTICS) } }

            searchBox.addTextChangedListener(object : TextWatcher {
                override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
                override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
                override fun afterTextChanged(s: Editable?) {
                    if (suppressSearchWatcher) return
                    val job = boundJob ?: return
                    searchQuery[job.number] = s?.toString().orEmpty()
                    renderDetailBody(job, RinExecutionManager.toSession(job))
                }
            })

            // Copy System (section 13): long-press anywhere on a finished run's card copies its
            // full output — quick access without needing a dedicated button on every row.
            itemView.setOnLongClickListener {
                val job = boundJob ?: return@setOnLongClickListener false
                if (job.status == JobStatus.QUEUED || job.status == JobStatus.RUNNING) return@setOnLongClickListener false
                copyToClipboard(
                    label = "Rin Run #${job.number}",
                    text = job.output,
                    toastRes = R.string.job_copied_toast
                )
                true
            }

            // شريط إجراءات سفلي مكتشَف (بديل صريح عن الضغط المطوَّل أعلاه): نسخ / مشاركة /
            // إعادة تشغيل — يظهر فقط لتشغيل مكتمل (انظر bind()).
            footerCopy.text = "📋  " + context.getString(R.string.job_copy_cta)
            footerCopy.setOnClickListener {
                val job = boundJob ?: return@setOnClickListener
                copyToClipboard(
                    label = "Rin Run #${job.number}",
                    text = job.output,
                    toastRes = R.string.job_copied_toast
                )
            }
            footerShare.text = "📤  " + context.getString(R.string.job_share_cta)
            footerShare.setOnClickListener {
                val job = boundJob ?: return@setOnClickListener
                shareText("Rin Run #${job.number}", job.output)
            }
            footerRerun.text = "↻  " + context.getString(R.string.job_rerun_cta)
            footerRerun.setOnClickListener {
                val job = boundJob ?: return@setOnClickListener
                onRerunRequested?.invoke(job.source)
                Toast.makeText(context, context.getString(R.string.job_rerun_toast, job.number), Toast.LENGTH_SHORT).show()
            }
        }

        fun bind(job: RinJob) {
            boundJob = job
            title.text = if (job.pinned) {
                context.getString(R.string.job_title_fmt, job.number) + " 📌"
            } else {
                context.getString(R.string.job_title_fmt, job.number)
            }

            pinBtn.text = if (job.pinned) "📌" else "📍"
            pinBtn.alpha = if (job.pinned) 1f else 0.45f
            pinBtn.contentDescription = context.getString(
                if (job.pinned) R.string.job_unpin_cta else R.string.job_pin_cta
            )
            pinBtn.setOnClickListener { onPinToggleRequested?.invoke(job.number) }

            val (label, colorRes) = when (job.status) {
                JobStatus.QUEUED -> context.getString(R.string.job_status_queued) to R.color.status_queued
                JobStatus.RUNNING -> context.getString(R.string.job_status_running) to R.color.status_running
                JobStatus.SUCCESS -> context.getString(R.string.job_status_success) to R.color.status_success
                JobStatus.ERROR -> context.getString(R.string.job_status_error) to R.color.status_error
                JobStatus.TIMEOUT -> context.getString(R.string.job_status_timeout) to R.color.status_timeout
                JobStatus.CANCELLED -> context.getString(R.string.job_status_cancelled) to R.color.status_cancelled
            }
            val color = ContextCompat.getColor(context, colorRes)
            statusText.text = label
            statusText.setTextColor(color)
            dot.backgroundTintList = android.content.res.ColorStateList.valueOf(color)

            val pillColor = android.graphics.Color.argb(
                40,
                android.graphics.Color.red(color),
                android.graphics.Color.green(color),
                android.graphics.Color.blue(color)
            )
            statusText.backgroundTintList = android.content.res.ColorStateList.valueOf(pillColor)

            duration.text = if (job.startedAt == 0L) "" else
                context.getString(R.string.job_duration_fmt, job.durationMs())
            time.text = TIME_FORMAT.format(Date(job.queuedAt))

            if (job.status == JobStatus.RUNNING) startDurationTicker(job) else stopDurationTicker()

            if (job.status == JobStatus.QUEUED) {
                cancelBtn.visibility = View.VISIBLE
                cancelBtn.setOnClickListener { onCancelRequested?.invoke(job.number) }
            } else {
                cancelBtn.visibility = View.GONE
                cancelBtn.setOnClickListener(null)
            }

            applyExpandState(isExpanded(job))

            val finished = job.status != JobStatus.QUEUED && job.status != JobStatus.RUNNING

            outputLines.removeAllViews()
            eventLines.removeAllViews()
            diagnosticsTab.removeAllViews()
            artifactsContainer.removeAllViews()
            artifactsContainer.visibility = View.GONE

            if (!finished) {
                statsDivider.visibility = View.GONE
                statsText.visibility = View.GONE

                // Tabs/search/filters only make sense once there's a finished session to slice
                // three ways -- a QUEUED/RUNNING job just shows its live stream, exactly as before.
                tabOutput.visibility = View.GONE
                tabEvents.visibility = View.GONE
                tabDiagnostics.visibility = View.GONE
                searchBox.visibility = View.GONE
                eventFilterScroll.visibility = View.GONE
                footerActions.visibility = View.GONE
                outputLines.visibility = View.VISIBLE
                eventLines.visibility = View.GONE
                diagnosticsTab.visibility = View.GONE

                when (job.status) {
                    JobStatus.QUEUED -> {
                        fallbackOutput.visibility = View.GONE
                        fallbackOutput.text = ""
                    }
                    else -> {
                        // Live Output (section 7): render whatever has streamed in so far, exactly
                        // the same icon+text rows the finished view uses, instead of a static "…"
                        // placeholder until the whole run completes.
                        if (job.liveLines.isEmpty()) {
                            fallbackOutput.visibility = View.VISIBLE
                            fallbackOutput.text = "…"
                        } else {
                            fallbackOutput.visibility = View.GONE
                            fallbackOutput.text = ""
                            for (line in job.liveLines) {
                                outputLines.addView(buildLineRow(line))
                            }
                        }
                    }
                }
                return
            }

            fallbackOutput.visibility = View.GONE
            fallbackOutput.text = ""

            // Real per-run numbers only (RinExecutionManager.RinExecutionStats) — duration,
            // event count, output size and artifact count all come from re-deriving this exact
            // job's actual output/events, not synthesized placeholders.
            val session = RinExecutionManager.toSession(job)
            statsDivider.visibility = View.VISIBLE
            statsText.visibility = View.VISIBLE
            statsText.text = context.getString(
                R.string.job_stats_fmt,
                session.stats.durationMs,
                session.stats.eventCount,
                RinConsoleFormatter.formatBytes(session.stats.outputBytes.toLong()),
                session.stats.artifactCount
            )

            tabOutput.visibility = View.VISIBLE
            tabEvents.visibility = View.VISIBLE
            tabDiagnostics.visibility = View.VISIBLE
            searchBox.visibility = View.VISIBLE
            footerActions.visibility = View.VISIBLE

            suppressSearchWatcher = true
            val storedQuery = searchQuery[job.number].orEmpty()
            if (searchBox.text?.toString() != storedQuery) {
                searchBox.setText(storedQuery)
                searchBox.setSelection(storedQuery.length)
            }
            suppressSearchWatcher = false

            updateTabHighlight(job, session)
            renderDetailBody(job, session)
        }

        /** Shows/hides [detailContainer] and rotates [expandIcon] to match, without touching any
         *  other bound state — cheap enough to call on every bind and on every header tap. */
        private fun applyExpandState(expanded: Boolean) {
            detailContainer.visibility = if (expanded) View.VISIBLE else View.GONE
            expandIcon.animate().rotation(if (expanded) 90f else 0f).setDuration(120L).start()
        }

        private fun selectTab(job: RinJob, tab: JobTab) {
            selectedTab[job.number] = tab
            val session = RinExecutionManager.toSession(job)
            updateTabHighlight(job, session)
            renderDetailBody(job, session)
        }

        /** Applies the selected/unselected pill style + real counts (section 10) to the three
         *  tab labels. Counts come straight from [session] -- the same object [renderDetailBody]
         *  renders from, so a tab's count and its content can never disagree. */
        private fun updateTabHighlight(job: RinJob, session: RinRunSession) {
            val tab = selectedTab[job.number] ?: JobTab.OUTPUT
            val outputCount = RinConsoleFormatter.formatLines(job.output).size

            tabOutput.text = context.getString(R.string.job_tab_output_fmt, outputCount)
            tabEvents.text = context.getString(R.string.job_tab_events_fmt, session.events.size)
            tabDiagnostics.text = context.getString(R.string.job_tab_diagnostics_fmt, session.diagnostics.size)

            fun style(view: TextView, active: Boolean) {
                view.setBackgroundResource(if (active) R.drawable.bg_category_chip else R.drawable.bg_filter_chip_unselected)
                view.setTextColor(
                    ContextCompat.getColor(context, if (active) R.color.rin_accent else R.color.rin_on_toolbar_dim)
                )
            }
            style(tabOutput, tab == JobTab.OUTPUT)
            style(tabEvents, tab == JobTab.EVENTS)
            style(tabDiagnostics, tab == JobTab.DIAGNOSTICS)
        }

        /** Redraws whichever tab's content container is active, applying the current search
         *  query (and, for Events, the current type filter chip). Called on bind, on every tab
         *  switch, and on every search keystroke -- never touches [headerRow]/[searchBox] itself
         *  so typing doesn't lose focus or cursor position. */
        private fun renderDetailBody(job: RinJob, session: RinRunSession) {
            val tab = selectedTab[job.number] ?: JobTab.OUTPUT
            val query = searchQuery[job.number].orEmpty().trim()

            outputLines.visibility = if (tab == JobTab.OUTPUT) View.VISIBLE else View.GONE
            eventLines.visibility = if (tab == JobTab.EVENTS) View.VISIBLE else View.GONE
            diagnosticsTab.visibility = if (tab == JobTab.DIAGNOSTICS) View.VISIBLE else View.GONE
            eventFilterScroll.visibility = if (tab == JobTab.EVENTS) View.VISIBLE else View.GONE

            when (tab) {
                JobTab.OUTPUT -> renderOutputTab(job, query)
                JobTab.EVENTS -> renderEventsTab(job, session, query)
                JobTab.DIAGNOSTICS -> renderDiagnosticsTab(job, session)
            }
        }

        /** Output tab: same per-line icon rows + artifact chips as before, now filtered by the
         *  real search query (substring match on the printed text, case-insensitive). */
        private fun renderOutputTab(job: RinJob, query: String) {
            outputLines.removeAllViews()
            val lines = RinConsoleFormatter.formatLines(job.output)
                .filter { query.isEmpty() || it.text.contains(query, ignoreCase = true) }
            if (lines.isEmpty() && query.isNotEmpty()) {
                outputLines.addView(buildEmptyStateRow(context.getString(R.string.job_no_results)))
            } else {
                for (line in lines) {
                    outputLines.addView(buildLineRow(line, query))
                }
            }

            // Diagnostics (section 5): when the engine reported a real structured diagnostic
            // (RinDiagnostic — code/line/column/hints, not text scraped back out of the console),
            // surface "Details" + "Copy" actions for it instead of leaving the person to parse the
            // rendered error text by eye.
            val diagnostic = job.diagnostic
            if (job.status == JobStatus.ERROR && diagnostic != null) {
                outputLines.addView(buildDiagnosticActionsRow(job, diagnostic))
            }

            artifactsContainer.removeAllViews()
            artifactsContainer.visibility = View.GONE
            if (job.status != JobStatus.SUCCESS) return
            val baseDir = try {
                RinEngine.currentBaseDir()
            } catch (t: Throwable) {
                ""
            }
            val artifacts = RinConsoleFormatter.extractArtifacts(job.output, baseDir)
            if (artifacts.isEmpty()) return

            artifactsContainer.visibility = View.VISIBLE
            val header = TextView(context).apply {
                text = context.getString(R.string.download_results_label)
                textSize = 11.5f
                setTypeface(typeface, Typeface.BOLD)
                setTextColor(ContextCompat.getColor(context, R.color.rin_on_toolbar_dim))
                setPadding(0, 0, 0, (6 * dp).toInt())
            }
            artifactsContainer.addView(header)
            artifacts.forEach { artifact ->
                artifactsContainer.addView(buildArtifactChip(artifact))
            }
        }

        /** Events tab (section 4/11): [RinExecutionEvent] rows -- includes RUN_STARTED/
         *  RUN_FINISHED markers that plain output lines don't have -- filtered by the selected
         *  type chip via [RinExecutionManager.filterEvents] and then by the search query. */
        private fun renderEventsTab(job: RinJob, session: RinRunSession, query: String) {
            buildEventFilterChips(job)

            eventLines.removeAllViews()
            val activeFilter = selectedEventFilter[job.number]
            val typed = if (activeFilter == null) session.events
            else RinExecutionManager.filterEvents(session, setOf(activeFilter))
            val filtered = typed.filter { query.isEmpty() || it.message.contains(query, ignoreCase = true) }

            if (filtered.isEmpty()) {
                eventLines.addView(buildEmptyStateRow(context.getString(R.string.job_no_results)))
                return
            }
            filtered.forEach { event ->
                eventLines.addView(buildLineRow(RinLogLine(event.level, event.message), query))
            }
        }

        /** Builds the ALL/OUTPUT/INFO/WARNING/ERROR/DEBUG filter chip row for the Events tab,
         *  highlighting whichever one is selected for this job. */
        private fun buildEventFilterChips(job: RinJob) {
            eventFilterChips.removeAllViews()
            val options: List<Pair<RinEventType?, Int>> = listOf(
                null to R.string.job_filter_all,
                RinEventType.OUTPUT to R.string.job_filter_output,
                RinEventType.INFO to R.string.job_filter_info,
                RinEventType.WARNING to R.string.job_filter_warning,
                RinEventType.ERROR to R.string.job_filter_error,
                RinEventType.DEBUG to R.string.job_filter_debug
            )
            val active = selectedEventFilter[job.number]
            options.forEach { (type, labelRes) ->
                val chip = TextView(context).apply {
                    text = context.getString(labelRes)
                    textSize = 11f
                    setTypeface(typeface, Typeface.BOLD)
                    setPadding((12 * dp).toInt(), (5 * dp).toInt(), (12 * dp).toInt(), (5 * dp).toInt())
                    setBackgroundResource(if (type == active) R.drawable.bg_category_chip else R.drawable.bg_filter_chip_unselected)
                    setTextColor(
                        ContextCompat.getColor(context, if (type == active) R.color.rin_accent else R.color.rin_on_toolbar_dim)
                    )
                    layoutParams = LinearLayout.LayoutParams(
                        LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT
                    ).apply { marginEnd = (6 * dp).toInt() }
                    isClickable = true
                    isFocusable = true
                    setOnClickListener {
                        selectedEventFilter[job.number] = type
                        renderDetailBody(job, RinExecutionManager.toSession(job))
                    }
                }
                eventFilterChips.addView(chip)
            }
        }

        /** Diagnostics tab (section 5/10): every real [RinDiagnostic] this run produced (almost
         *  always zero or one), or an explicit empty state instead of a blank panel. */
        private fun renderDiagnosticsTab(job: RinJob, session: RinRunSession) {
            diagnosticsTab.removeAllViews()
            if (session.diagnostics.isEmpty()) {
                diagnosticsTab.addView(buildEmptyStateRow(context.getString(R.string.job_no_diagnostics)))
                return
            }
            session.diagnostics.forEach { diagnostic ->
                val summary = TextView(context).apply {
                    text = "${diagnostic.severity.uppercase()} [${diagnostic.code}] ${diagnostic.message}\n" +
                        "${diagnostic.file}:${diagnostic.line}:${diagnostic.column}"
                    textSize = 12.5f
                    typeface = Typeface.MONOSPACE
                    setTextColor(ContextCompat.getColor(context, R.color.log_kind_error))
                    setTextIsSelectable(true)
                    setPadding(0, (4 * dp).toInt(), 0, 0)
                }
                diagnosticsTab.addView(summary)
                diagnosticsTab.addView(buildDiagnosticActionsRow(job, diagnostic))
            }
        }

        private fun buildEmptyStateRow(text: String): View = TextView(context).apply {
            this.text = text
            textSize = 12f
            setTextColor(ContextCompat.getColor(context, R.color.rin_on_toolbar_dim))
            setPadding(0, (4 * dp).toInt(), 0, (4 * dp).toInt())
        }

        private fun buildLineRow(line: RinLogLine, highlightQuery: String = ""): View {
            val row = LinearLayout(context).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = android.view.Gravity.TOP
                setPadding(0, (2 * dp).toInt(), 0, (2 * dp).toInt())
            }
            val tint = ContextCompat.getColor(context, line.kind.colorRes)

            if (line.kind.icon != null) {
                val icon = ImageView(context).apply {
                    setImageResource(line.kind.icon)
                    imageTintList = android.content.res.ColorStateList.valueOf(tint)
                    layoutParams = LinearLayout.LayoutParams((16 * dp).toInt(), (16 * dp).toInt()).apply {
                        topMargin = (2 * dp).toInt()
                        marginEnd = (8 * dp).toInt()
                    }
                }
                row.addView(icon)
            } else {
                // سطر نص عادي (مثلاً ناتج print مباشر) بلا أيقونة، لكن بمسافة بادئة موحّدة
                val spacer = View(context).apply {
                    layoutParams = LinearLayout.LayoutParams((16 * dp).toInt(), (16 * dp).toInt()).apply {
                        marginEnd = (8 * dp).toInt()
                    }
                }
                row.addView(spacer)
            }

            val text = TextView(context).apply {
                text = highlightMatches(line.text, highlightQuery)
                textSize = 12.5f
                typeface = Typeface.MONOSPACE
                setTextColor(if (line.kind == LogKind.PLAIN) ContextCompat.getColor(context, R.color.rin_console_text) else tint)
                setTextIsSelectable(true)
                layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
            }
            row.addView(text)
            return row
        }

        /** يُبرِز كل تطابقات [query] داخل [text] (خلفية صفراء + عريض) بدل الاكتفاء بترشيح
         *  الأسطر التي تحتويه — حتى يرى المستخدم فوراً أين بالضبط طابق البحث داخل سطر طويل،
         *  تمامًا كتمييز نتائج البحث في أي محرر نصوص احترافي. يُرجِع [text] كما هو إن كان
         *  [query] فارغاً (لا بحث نشط). */
        private fun highlightMatches(text: String, query: String): CharSequence {
            if (query.isBlank()) return text
            val spannable = SpannableString(text)
            var start = text.indexOf(query, 0, ignoreCase = true)
            val highlightColor = ContextCompat.getColor(context, R.color.rin_accent)
            while (start >= 0) {
                val end = start + query.length
                spannable.setSpan(
                    BackgroundColorSpan(android.graphics.Color.argb(70, android.graphics.Color.red(highlightColor), android.graphics.Color.green(highlightColor), android.graphics.Color.blue(highlightColor))),
                    start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
                )
                spannable.setSpan(StyleSpan(Typeface.BOLD), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                start = text.indexOf(query, end, ignoreCase = true)
            }
            return spannable
        }

        private fun buildArtifactChip(artifact: RinArtifact): View {
            val chip = LinearLayout(context).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = android.view.Gravity.CENTER_VERTICAL
                setBackgroundResource(R.drawable.bg_download_chip)
                setPadding((10 * dp).toInt(), (8 * dp).toInt(), (10 * dp).toInt(), (8 * dp).toInt())
                layoutParams = LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT
                ).apply { topMargin = (6 * dp).toInt() }
                isClickable = true
                isFocusable = true
            }

            val icon = ImageView(context).apply {
                setImageResource(artifact.kind.icon)
                imageTintList = android.content.res.ColorStateList.valueOf(
                    ContextCompat.getColor(context, R.color.log_kind_export)
                )
                layoutParams = LinearLayout.LayoutParams((18 * dp).toInt(), (18 * dp).toInt()).apply {
                    marginEnd = (10 * dp).toInt()
                }
            }
            chip.addView(icon)

            val labelColumn = LinearLayout(context).apply {
                orientation = LinearLayout.VERTICAL
                layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
            }
            val nameView = TextView(context).apply {
                text = artifact.displayName
                textSize = 12.5f
                typeface = Typeface.MONOSPACE
                setTextColor(ContextCompat.getColor(context, R.color.rin_on_toolbar))
            }
            val sizeView = TextView(context).apply {
                text = RinConsoleFormatter.formatBytes(artifact.sizeBytes)
                textSize = 10.5f
                setTextColor(ContextCompat.getColor(context, R.color.rin_editor_hint))
            }
            labelColumn.addView(nameView)
            labelColumn.addView(sizeView)
            chip.addView(labelColumn)

            val downloadIcon = ImageView(context).apply {
                setImageResource(R.drawable.ic_log_download)
                imageTintList = android.content.res.ColorStateList.valueOf(
                    ContextCompat.getColor(context, R.color.rin_accent)
                )
                layoutParams = LinearLayout.LayoutParams((18 * dp).toInt(), (18 * dp).toInt())
            }
            chip.addView(downloadIcon)

            chip.setOnClickListener { requestDownload(artifact) }
            return chip
        }

        /** Small "Details" / "Copy" action row shown under an ERROR job's output, section 5. */
        private fun buildDiagnosticActionsRow(job: RinJob, diagnostic: RinDiagnostic): View {
            val row = LinearLayout(context).apply {
                orientation = LinearLayout.HORIZONTAL
                setPadding(0, (6 * dp).toInt(), 0, 0)
            }

            val detailsBtn = TextView(context).apply {
                text = context.getString(R.string.job_details_cta)
                textSize = 11.5f
                setTypeface(typeface, Typeface.BOLD)
                setTextColor(ContextCompat.getColor(context, R.color.rin_accent))
                setPadding(0, 0, (16 * dp).toInt(), 0)
                isClickable = true
                isFocusable = true
                setOnClickListener { showDiagnosticDetails(job, diagnostic) }
            }
            row.addView(detailsBtn)

            val copyBtn = TextView(context).apply {
                text = context.getString(R.string.job_copy_cta)
                textSize = 11.5f
                setTypeface(typeface, Typeface.BOLD)
                setTextColor(ContextCompat.getColor(context, R.color.rin_accent))
                isClickable = true
                isFocusable = true
                setOnClickListener {
                    copyToClipboard(
                        label = "Rin Diagnostic ${diagnostic.code}",
                        text = diagnosticAsMarkdown(job, diagnostic),
                        toastRes = R.string.job_copied_toast
                    )
                }
            }
            row.addView(copyBtn)
            return row
        }

        /** Full-detail dialog for one diagnostic — the "[ Details ]" action from section 5. */
        private fun showDiagnosticDetails(job: RinJob, d: RinDiagnostic) {
            val body = buildString {
                append(context.getString(R.string.job_title_fmt, job.number)).append("\n\n")
                append(d.severity.uppercase()).append(" [").append(d.code).append("] ").append(d.message).append("\n")
                append(d.file).append(":").append(d.line).append(":").append(d.column).append("\n")
                d.reason?.let { append("\n").append(context.getString(R.string.diag_reason_label)).append(": ").append(it).append("\n") }
                d.expected?.let { append("\n").append(context.getString(R.string.diag_expected_label)).append(": ").append(it).append("\n") }
                d.found?.let { append(context.getString(R.string.diag_found_label)).append(": ").append(it).append("\n") }
                if (d.suggestions.isNotEmpty()) {
                    append("\n").append(context.getString(R.string.diag_suggestions_label)).append(":\n")
                    d.suggestions.forEach { append("  • ").append(it).append("\n") }
                }
                if (d.hints.isNotEmpty()) {
                    append("\n").append(context.getString(R.string.diag_hint_label)).append(":\n")
                    d.hints.forEach { append("  ").append(it).append("\n") }
                }
                if (d.notes.isNotEmpty()) {
                    append("\n").append(context.getString(R.string.diag_notes_label)).append(":\n")
                    d.notes.forEach { append("  ").append(it).append("\n") }
                }
            }

            AlertDialog.Builder(context)
                .setTitle("${d.code} — ${d.codeName}")
                .setMessage(body)
                .setPositiveButton(context.getString(R.string.job_copy_cta)) { _, _ ->
                    copyToClipboard("Rin Diagnostic ${d.code}", body, R.string.job_copied_toast)
                }
                .setNegativeButton(android.R.string.cancel, null)
                .show()
        }

        /** "Copy as Markdown" body for a diagnostic — section 13. */
        private fun diagnosticAsMarkdown(job: RinJob, d: RinDiagnostic): String = buildString {
            append("### Rin Run #${job.number} — ${d.severity} ${d.code}\n\n")
            append("**${d.message}**\n\n")
            append("`${d.file}:${d.line}:${d.column}`\n")
            d.reason?.let { append("\n> reason: $it\n") }
            if (d.hints.isNotEmpty()) {
                append("\n**help:**\n")
                d.hints.forEach { append("- $it\n") }
            }
        }

        private fun copyToClipboard(label: String, text: String, toastRes: Int) {
            val clipboard = context.getSystemService(Context.CLIPBOARD_SERVICE) as? ClipboardManager
            if (clipboard == null || text.isEmpty()) return
            clipboard.setPrimaryClip(ClipData.newPlainText(label, text))
            Toast.makeText(context, context.getString(toastRes), Toast.LENGTH_SHORT).show()
        }

        /** يفتح مُنتقي المشاركة القياسي في أندرويد (Share sheet) بناتج هذا التشغيل كنص خام —
         *  إضافةً إلى النسخ السريع، حتى يمكن إرسال ناتج التشغيل مباشرةً عبر البريد/الرسائل/أي
         *  تطبيق آخر بدون لصق يدوي أولاً. لا شيء يُرسَل هنا إلا عند اختيار المستخدم صراحةً وجهةً. */
        private fun shareText(subject: String, text: String) {
            if (text.isEmpty()) return
            val send = Intent(Intent.ACTION_SEND).apply {
                type = "text/plain"
                putExtra(Intent.EXTRA_SUBJECT, subject)
                putExtra(Intent.EXTRA_TEXT, text)
            }
            val chooser = Intent.createChooser(send, subject)
            if (context !is Activity) chooser.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            context.startActivity(chooser)
        }

        /** يبدأ عدّاداً حياً يُحدِّث [duration] كل 500ms من [RinJob.durationMs] الحقيقي طوال
         *  بقاء [job] في حالة RUNNING — بدل انتظار وصول جزء ناتج جديد لتحديث المدة، وهو ما كان
         *  يترك المدة "مجمَّدة" ظاهرياً أثناء حساب طويل صامت. يتوقّف من تلقاء نفسه إن لم يعد
         *  [boundJob] هو نفسه [job] (أُعيد تدوير الصفّ لعنصر آخر) أو إن لم تعد حالته RUNNING. */
        private fun startDurationTicker(job: RinJob) {
            if (ticker != null) return // مُشغَّل مسبقاً لنفس الصفّ
            val tick = object : Runnable {
                override fun run() {
                    val current = boundJob
                    if (current !== job || current.status != JobStatus.RUNNING) {
                        ticker = null
                        return
                    }
                    duration.text = context.getString(R.string.job_duration_fmt, job.durationMs())
                    tickerHandler.postDelayed(this, 500L)
                }
            }
            ticker = tick
            tickerHandler.postDelayed(tick, 500L)
        }

        fun stopDurationTicker() {
            ticker?.let { tickerHandler.removeCallbacks(it) }
            ticker = null
        }

        private fun requestDownload(artifact: RinArtifact) {
            val activity = context as? Activity
            if (activity == null) {
                Toast.makeText(context, context.getString(R.string.download_error_toast, "no activity"), Toast.LENGTH_SHORT).show()
                return
            }

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M && Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) {
                val granted = ContextCompat.checkSelfPermission(
                    activity, Manifest.permission.WRITE_EXTERNAL_STORAGE
                ) == PackageManager.PERMISSION_GRANTED
                if (!granted) {
                    ActivityCompat.requestPermissions(
                        activity, arrayOf(Manifest.permission.WRITE_EXTERNAL_STORAGE), 4201
                    )
                    Toast.makeText(
                        context, context.getString(R.string.download_permission_needed_toast), Toast.LENGTH_LONG
                    ).show()
                    return
                }
            }

            val progressDialog = RinDownloadProgressDialog(activity, artifact.displayName)
            progressDialog.show()

            RinDownloadManager.downloadToPublicDownloads(
                activity = activity,
                artifact = artifact,
                onProgress = { copied, total -> progressDialog.updateProgress(copied, total) },
                onDone = { uri, error ->
                    if (error != null || uri == null) {
                        progressDialog.finishError(
                            context.getString(R.string.download_error_toast, error?.message ?: "?")
                        )
                        Handler(Looper.getMainLooper()).postDelayed({ progressDialog.dismiss() }, 1400)
                        return@downloadToPublicDownloads
                    }
                    progressDialog.finishSuccess(context.getString(R.string.download_status_done))
                    Toast.makeText(
                        context,
                        context.getString(R.string.download_done_toast, artifact.displayName),
                        Toast.LENGTH_SHORT
                    ).show()
                    Handler(Looper.getMainLooper()).postDelayed({
                        progressDialog.dismiss()
                        // "يتم تنفيذه": يفتح الملف المنزَّل مباشرةً بالتطبيق المناسب على الجهاز
                        RinDownloadManager.openDownloadedFile(activity, uri, artifact.kind.mime)
                    }, 500)
                }
            )
        }
    }
}
