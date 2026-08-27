package com.dlof.rinlang

/**
 * Category of one [RinExecutionEvent] (section 4/17: Structured Events / Rin-aware Output).
 *
 * Derived from [LogKind] -- which is itself derived from the exact prefixes
 * `rin_interpreter.cpp` writes (see [RinConsoleFormatter.PREFIX_ORDER]) -- rather than from a
 * second independent classifier. That keeps this a single source of truth: if the native engine
 * ever exposes event kinds directly instead of through printed prefixes, only
 * [RinExecutionManager.eventTypeFor] needs to change, not every consumer of [RinRunSession].
 */
enum class RinEventType {
    RUN_STARTED, OUTPUT, DEBUG, INFO, WARNING, ERROR,
    CONTAINER, VOLUME, IMPORT, EXPORT, FILE, PIPELINE, VIEW, NETWORK, ARTIFACT, RETURN,
    RUN_FINISHED
}

/**
 * One structured execution event (section 4). [line]/[column] are only populated for the
 * diagnostic-derived ERROR event -- per-statement output lines don't carry their own
 * line/column yet because `Interpreter::run`'s streaming callback (see
 * [RinEngine.RinStreamListener]) reports finished text, not source position; this is honestly
 * left null rather than guessed. Same for [durationMs] on per-line events: only the synthesized
 * RUN_FINISHED event has a real duration ([RinJob.durationMs]).
 */
data class RinExecutionEvent(
    val sequence: Int,
    val timestamp: Long,
    val type: RinEventType,
    val level: LogKind,
    val message: String,
    val line: Int? = null,
    val column: Int? = null,
    val durationMs: Long? = null,
    val metadata: Map<String, String> = emptyMap()
)

/** Real per-run numbers only (section 14) -- never a placeholder for a metric the engine/JVM
 *  doesn't actually expose (e.g. no CPU time or native memory usage here; nothing invents one). */
data class RinExecutionStats(
    val durationMs: Long,
    val eventCount: Int,
    val outputBytes: Int,
    val artifactCount: Int
)

/**
 * One [RinJob] re-projected as OUTPUT + EVENTS + DIAGNOSTICS, so the Code Output UI (tabs,
 * filters, search, stats -- sections 10/11/12/14) has one shape to read instead of separately
 * calling [RinConsoleFormatter] and reaching into [RinJob] fields itself.
 *
 * This is a read-only view: [RinJobScheduler] stays the single owner of job lifecycle
 * (queueing, running, cancelling, pinning, history trimming). Nothing here duplicates that
 * state or re-implements the queue (section 21: no unnecessary competing layers).
 */
data class RinRunSession(
    val job: RinJob,
    val events: List<RinExecutionEvent>,
    val diagnostics: List<RinDiagnostic>,
    val stats: RinExecutionStats
)

/**
 * Central layer tying [RinJobScheduler] (Queue), [RinExecutionEvent] (Events) and
 * [RinDiagnostic] (Diagnostics) together into [RinRunSession]s (section 21/30 architecture:
 * Rin Engine -> Structured Execution Events -> Rin Run Session -> Rin Run Queue -> Code Output
 * Renderer). Deliberately a thin facade over the existing [RinJobScheduler] rather than a
 * replacement for it -- the scheduler already does FIFO execution, cancellation, timeout,
 * pinning and bounded history correctly; re-implementing that here would be exactly the
 * "unnecessary new layer" section 21 warns against.
 */
object RinExecutionManager {

    /** Latest sessions, delivered on the main thread. Prefer [attach] over setting this
     *  directly so [detach] can cleanly undo the wiring later. */
    var onSessionsChanged: ((List<RinRunSession>) -> Unit)? = null

    private var installedHook: ((List<RinJob>) -> Unit)? = null
    private var previousHook: ((List<RinJob>) -> Unit)? = null

    /**
     * Starts forwarding [RinJobScheduler]'s job-list changes to [listener] as [RinRunSession]s.
     * Chains onto whatever [RinJobScheduler.onJobsChanged] already held instead of clobbering
     * it, and [detach] restores that exact previous value -- so repeated attach()/detach()
     * across Activity recreation (rotation, section 26 test #18) never stacks another wrapper
     * closure on top of the last one; it stays a single hook no matter how many times a
     * short-lived Activity attaches and detaches.
     *
     * Call once (e.g. from `onCreate`/`onStart`), and call [detach] from `onDestroy`.
     */
    fun attach(listener: (List<RinRunSession>) -> Unit) {
        onSessionsChanged = listener
        if (installedHook != null) return // already wired into the scheduler; swapping the
                                           // listener above is all a re-attach needs to do
        previousHook = RinJobScheduler.onJobsChanged
        val hook: (List<RinJob>) -> Unit = { jobs ->
            previousHook?.invoke(jobs)
            onSessionsChanged?.invoke(jobs.map { toSession(it) })
        }
        installedHook = hook
        RinJobScheduler.onJobsChanged = hook
    }

    /** Undoes [attach]: restores [RinJobScheduler.onJobsChanged] to exactly what it was before,
     *  so a destroyed Activity leaves no trace behind in the scheduler it doesn't own. */
    fun detach() {
        if (RinJobScheduler.onJobsChanged === installedHook) {
            RinJobScheduler.onJobsChanged = previousHook
        }
        installedHook = null
        previousHook = null
        onSessionsChanged = null
    }

    // ---- Queue passthroughs (RinJobScheduler remains the single source of truth) ----

    fun submit(source: String): RinJob? = RinJobScheduler.submit(source)
    fun cancel(number: Int): Boolean = RinJobScheduler.cancel(number)
    fun togglePin(number: Int): Boolean? = RinJobScheduler.togglePin(number)
    fun clear() = RinJobScheduler.clear()

    fun snapshot(): List<RinRunSession> = RinJobScheduler.snapshot().map { toSession(it) }

    /** Sessions whose job is pinned (section 9), most recent first is left to the caller's own
     *  sort -- this just applies the filter. */
    fun pinnedSessions(): List<RinRunSession> = snapshot().filter { it.job.pinned }

    /** Every real diagnostic across current job history, most recent first -- feeds the
     *  DIAGNOSTICS tab (section 10) without the UI walking every job's console output itself.
     *  A job with no [RinJob.diagnostic] contributes nothing; never synthesized. */
    fun allDiagnostics(): List<Pair<RinJob, RinDiagnostic>> =
        RinJobScheduler.snapshot().asReversed().mapNotNull { job -> job.diagnostic?.let { job to it } }

    /**
     * Re-slices [session]'s events down to [types] -- backs the Events tab's
     * ALL/OUTPUT/INFO/WARNING/ERROR/DEBUG/FILES/PIPELINE filter chips (section 11) as a pure
     * in-memory filter over already-computed events, so switching a filter never re-runs the
     * program. An empty [types] means "no filter", i.e. ALL.
     */
    fun filterEvents(session: RinRunSession, types: Set<RinEventType>): List<RinExecutionEvent> {
        if (types.isEmpty()) return session.events
        return session.events.filter { it.type in types }
    }

    /**
     * Projects one [job] into a [RinRunSession]. Safe to call from any thread: it only reads
     * [job]'s already-`@Volatile` fields and re-derives everything from [RinJob.output] /
     * [RinJob.liveLines] through the existing [RinConsoleFormatter] -- nothing is cached here,
     * so a session built mid-RUNNING and one built after FINISHED are both a true snapshot of
     * that moment, never stale state left over from an earlier call.
     */
    fun toSession(job: RinJob): RinRunSession {
        val lines: List<RinLogLine> =
            if (job.status == JobStatus.RUNNING) job.liveLines
            else RinConsoleFormatter.formatLines(job.output)

        val events = ArrayList<RinExecutionEvent>(lines.size + 3)
        var seq = 0

        events += RinExecutionEvent(
            sequence = ++seq,
            timestamp = job.queuedAt,
            type = RinEventType.RUN_STARTED,
            level = LogKind.PLAIN,
            message = "run #${job.number} queued"
        )
        if (job.startedAt != 0L) {
            events += RinExecutionEvent(
                sequence = ++seq,
                timestamp = job.startedAt,
                type = RinEventType.RUN_STARTED,
                level = LogKind.PLAIN,
                message = "run #${job.number} started"
            )
        }

        lines.forEach { line ->
            events += RinExecutionEvent(
                sequence = ++seq,
                // Per-statement output isn't individually timestamped by the native streaming
                // callback (it reports text, not wall-clock time per chunk) -- job.startedAt is
                // the most accurate honest value available rather than inventing per-line times.
                timestamp = job.startedAt,
                type = eventTypeFor(line.kind),
                level = line.kind,
                message = line.text
            )
        }

        job.diagnostic?.let { diag ->
            events += RinExecutionEvent(
                sequence = ++seq,
                timestamp = job.finishedAt.takeIf { it != 0L } ?: job.startedAt,
                type = RinEventType.ERROR,
                level = LogKind.ERROR,
                message = diag.message,
                line = diag.line.takeIf { it > 0 },
                column = diag.column.takeIf { it > 0 },
                metadata = mapOf("code" to diag.code)
            )
        }

        if (job.finishedAt != 0L) {
            events += RinExecutionEvent(
                sequence = ++seq,
                timestamp = job.finishedAt,
                type = RinEventType.RUN_FINISHED,
                level = LogKind.PLAIN,
                message = "run #${job.number} finished: ${job.status}",
                durationMs = job.durationMs()
            )
        }

        val stats = RinExecutionStats(
            durationMs = job.durationMs(),
            eventCount = events.size,
            outputBytes = job.output.toByteArray(Charsets.UTF_8).size,
            artifactCount = RinConsoleFormatter.extractArtifacts(job.output, RinEngine.currentBaseDir()).size
        )

        return RinRunSession(
            job = job,
            events = events,
            diagnostics = listOfNotNull(job.diagnostic),
            stats = stats
        )
    }

    /** Maps a printed line's [LogKind] (see [RinConsoleFormatter.PREFIX_ORDER]) to the
     *  execution-event vocabulary from section 4. Exhaustive `when` on purpose: if [LogKind]
     *  gains a case, this fails to compile instead of silently defaulting it to OUTPUT. */
    private fun eventTypeFor(kind: LogKind): RinEventType = when (kind) {
        LogKind.ERROR -> RinEventType.ERROR
        LogKind.WARNING -> RinEventType.WARNING
        LogKind.INFO -> RinEventType.INFO
        LogKind.DEBUG -> RinEventType.DEBUG
        LogKind.SUCCESS -> RinEventType.OUTPUT
        LogKind.STRUCTURE, LogKind.GRID -> RinEventType.CONTAINER
        LogKind.DOC_INSERT, LogKind.DOC_UPDATE -> RinEventType.VOLUME
        LogKind.IMPORT, LogKind.LINK -> RinEventType.IMPORT
        LogKind.EXPORT, LogKind.IMAGE, LogKind.ARCHIVE -> RinEventType.ARTIFACT
        LogKind.FILE -> RinEventType.FILE
        LogKind.STYLE -> RinEventType.VIEW
        LogKind.NETWORK -> RinEventType.NETWORK
        LogKind.PLAIN -> RinEventType.OUTPUT
    }
}
