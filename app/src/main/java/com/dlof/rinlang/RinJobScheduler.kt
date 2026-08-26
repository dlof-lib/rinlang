package com.dlof.rinlang

import android.os.Handler
import android.os.Looper
import java.util.Collections
import java.util.concurrent.Callable
import java.util.concurrent.Executors
import java.util.concurrent.RejectedExecutionException
import java.util.concurrent.ThreadFactory
import java.util.concurrent.TimeUnit
import java.util.concurrent.TimeoutException
import java.util.concurrent.atomic.AtomicInteger

/**
 * Runs Rin programs the way the language's own `container`/`Volume` blocks
 * organize data: as discrete, ordered, independently-tracked units rather
 * than one shared blob.
 *
 * - [queueExecutor] is a single background thread, so runs never overlap or
 *   block the UI — each "Run" tap is queued and executed in order.
 * - Each individual run is dispatched onto [workerPool] and bounded by
 *   [TIMEOUT_MS], so one runaway program (e.g. an infinite `while`) can't
 *   freeze the whole queue forever; it's marked TIMEOUT and the scheduler
 *   moves on to the next job. The native call itself has no way to be
 *   interrupted mid-execution, so the abandoned native thread is simply left
 *   to finish on its own — safe here because the Rin engine keeps no shared
 *   global state between calls (see rin_interpreter.cpp).
 * - [jobs] is trimmed to [MAX_HISTORY] finished entries so a long-lived
 *   session (many Run taps over hours) can't grow the in-memory history —
 *   and therefore the RecyclerView backing list — without bound.
 * - A job still sitting in [JobStatus.QUEUED] can be pulled out of the
 *   queue with [cancel] before the scheduler thread ever reaches it, so the
 *   already-shipped `CANCELLED` status is actually reachable from the UI
 *   instead of being dead code.
 * - [MAX_PENDING] caps how many QUEUED-or-RUNNING jobs can exist at once,
 *   so rapid repeated taps on "Run" can't pile up an unbounded backlog of
 *   work behind the single-threaded [queueExecutor].
 */
object RinJobScheduler {

    private const val TIMEOUT_MS = 15_000L

    /** Upper bound on finished (terminal) jobs kept in memory/history. */
    private const val MAX_HISTORY = 200

    /** Upper bound on jobs that are queued or actively running at once. */
    private const val MAX_PENDING = 50

    private val queueExecutor = Executors.newSingleThreadExecutor(namedThreadFactory("RinJobQueue"))
    private val workerPool = Executors.newCachedThreadPool(namedThreadFactory("RinJobWorker"))
    private val mainHandler = Handler(Looper.getMainLooper())
    private val counter = AtomicInteger(0)
    private val jobs = Collections.synchronizedList(mutableListOf<RinJob>())

    /** Invoked on the main thread whenever the job list changes. */
    var onJobsChanged: ((List<RinJob>) -> Unit)? = null

    /**
     * Queues [source] for execution. Returns `null` (instead of a job) when
     * [MAX_PENDING] is already reached, so a caller mashing the Run button
     * gets clear back-pressure instead of an ever-growing silent backlog.
     */
    fun submit(source: String): RinJob? {
        synchronized(jobs) {
            val pending = jobs.count { it.status == JobStatus.QUEUED || it.status == JobStatus.RUNNING }
            if (pending >= MAX_PENDING) return null
        }

        val job = RinJob(number = counter.incrementAndGet(), source = source)
        jobs.add(job)
        trimHistoryLocked()
        notifyChanged()

        try {
            queueExecutor.submit { runJob(job) }
        } catch (e: RejectedExecutionException) {
            job.status = JobStatus.ERROR
            job.output = "[Fatal error]: job queue rejected this run: ${e.message}"
            job.finishedAt = System.currentTimeMillis()
            notifyChanged()
        }
        return job
    }

    /**
     * Cancels the job identified by [number] while it is still [JobStatus.QUEUED].
     * Returns `true` if the cancellation took effect. A job that has already
     * moved to RUNNING (or finished) cannot be cancelled here — the native
     * engine call has no interruption hook, so the honest contract is
     * "cancel only what hasn't started yet".
     */
    fun cancel(number: Int): Boolean {
        val job = synchronized(jobs) { jobs.find { it.number == number } } ?: return false
        val cancelled = synchronized(job) {
            if (job.status != JobStatus.QUEUED) {
                false
            } else {
                job.status = JobStatus.CANCELLED
                job.finishedAt = System.currentTimeMillis()
                job.output = "[Cancelled]: removed from the queue before it started running"
                true
            }
        }
        if (cancelled) notifyChanged()
        return cancelled
    }

    fun clear() {
        synchronized(jobs) { jobs.clear() }
        notifyChanged()
    }

    fun snapshot(): List<RinJob> = synchronized(jobs) { jobs.toList() }

    /** Detaches the UI listener. Call from `onDestroy` so a destroyed Activity is never retained. */
    fun detach() {
        onJobsChanged = null
    }

    private fun runJob(job: RinJob) {
        // The job may have been cancelled while it was waiting in line.
        synchronized(job) {
            if (job.status == JobStatus.CANCELLED) return
            job.status = JobStatus.RUNNING
            job.startedAt = System.currentTimeMillis()
        }
        notifyChanged()

        val future = workerPool.submit(Callable { RinEngine.runSource(job.source) })
        try {
            val result = future.get(TIMEOUT_MS, TimeUnit.MILLISECONDS)
            synchronized(job) {
                job.output = result
                job.status = if (result.startsWith("[")) JobStatus.ERROR else JobStatus.SUCCESS
            }
        } catch (e: TimeoutException) {
            future.cancel(true)
            synchronized(job) {
                job.output = "[Timeout]: execution exceeded ${TIMEOUT_MS / 1000} seconds and was abandoned"
                job.status = JobStatus.TIMEOUT
            }
        } catch (t: Throwable) {
            synchronized(job) {
                job.output = "[Fatal error]: ${t.message}"
                job.status = JobStatus.ERROR
            }
        }
        synchronized(job) { job.finishedAt = System.currentTimeMillis() }
        notifyChanged()
    }

    /** Drops the oldest *finished* jobs once history exceeds [MAX_HISTORY]; queued/running jobs are never dropped. */
    private fun trimHistoryLocked() {
        synchronized(jobs) {
            var overflow = jobs.size - MAX_HISTORY
            if (overflow <= 0) return
            val iterator = jobs.iterator()
            while (iterator.hasNext() && overflow > 0) {
                val candidate = iterator.next()
                if (candidate.status != JobStatus.QUEUED && candidate.status != JobStatus.RUNNING) {
                    iterator.remove()
                    overflow--
                }
            }
        }
    }

    private fun notifyChanged() {
        val snap = snapshot()
        mainHandler.post { onJobsChanged?.invoke(snap) }
    }

    private fun namedThreadFactory(prefix: String): ThreadFactory {
        val n = AtomicInteger(0)
        return ThreadFactory { r ->
            Thread(r, "$prefix-${n.incrementAndGet()}").apply { isDaemon = true }
        }
    }
}
