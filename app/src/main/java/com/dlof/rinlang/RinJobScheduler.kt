package com.dlof.rinlang

import android.os.Handler
import android.os.Looper
import java.util.Collections
import java.util.concurrent.Callable
import java.util.concurrent.Executors
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
 */
object RinJobScheduler {

    private const val TIMEOUT_MS = 15_000L

    private val queueExecutor = Executors.newSingleThreadExecutor()
    private val workerPool = Executors.newCachedThreadPool()
    private val mainHandler = Handler(Looper.getMainLooper())
    private val counter = AtomicInteger(0)
    private val jobs = Collections.synchronizedList(mutableListOf<RinJob>())

    /** Invoked on the main thread whenever the job list changes. */
    var onJobsChanged: ((List<RinJob>) -> Unit)? = null

    fun submit(source: String): RinJob {
        val job = RinJob(number = counter.incrementAndGet(), source = source)
        jobs.add(job)
        notifyChanged()
        queueExecutor.submit { runJob(job) }
        return job
    }

    fun clear() {
        jobs.clear()
        notifyChanged()
    }

    fun snapshot(): List<RinJob> = synchronized(jobs) { jobs.toList() }

    private fun runJob(job: RinJob) {
        job.status = JobStatus.RUNNING
        job.startedAt = System.currentTimeMillis()
        notifyChanged()

        val future = workerPool.submit(Callable { RinEngine.runSource(job.source) })
        try {
            val result = future.get(TIMEOUT_MS, TimeUnit.MILLISECONDS)
            job.output = result
            job.status = if (result.startsWith("[")) JobStatus.ERROR else JobStatus.SUCCESS
        } catch (e: TimeoutException) {
            future.cancel(true)
            job.output = "[Timeout]: execution exceeded ${TIMEOUT_MS / 1000} seconds and was abandoned"
            job.status = JobStatus.TIMEOUT
        } catch (t: Throwable) {
            job.output = "[Fatal error]: ${t.message}"
            job.status = JobStatus.ERROR
        }
        job.finishedAt = System.currentTimeMillis()
        notifyChanged()
    }

    private fun notifyChanged() {
        val snap = snapshot()
        mainHandler.post { onJobsChanged?.invoke(snap) }
    }
}
