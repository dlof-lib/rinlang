package com.dlof.rinlang

/** Lifecycle state of one queued/executed Rin program. */
enum class JobStatus { QUEUED, RUNNING, SUCCESS, ERROR, TIMEOUT, CANCELLED }

/**
 * A single scheduled run of a Rin program.
 *
 * The engine itself organizes data into named containers (`@container=...`);
 * on the app side we mirror that same idea for *executions*: every tap of
 * "Run" becomes its own tracked, timestamped entry that moves through a
 * queue instead of just overwriting one shared console.
 */
data class RinJob(
    val number: Int,
    val source: String,
    @Volatile var status: JobStatus = JobStatus.QUEUED,
    @Volatile var queuedAt: Long = System.currentTimeMillis(),
    @Volatile var startedAt: Long = 0L,
    @Volatile var finishedAt: Long = 0L,
    @Volatile var output: String = "",
    /** Structured diagnostic for this run's failure, when the engine reported one (see
     *  [RinEngine.runSourceStructured]); null on success or for errors without a rich diagnostic
     *  (e.g. a queue-level rejection). Populated by [RinJobScheduler], never inferred from [output]. */
    @Volatile var diagnostic: RinDiagnostic? = null,
    /** Pinned runs are exempt from [RinJobScheduler]'s history trimming (see section 9: Pinned Runs). */
    @Volatile var pinned: Boolean = false
) {
    /** Wall-clock duration of the run so far, in milliseconds. */
    fun durationMs(): Long {
        if (startedAt == 0L) return 0L
        val end = if (finishedAt != 0L) finishedAt else System.currentTimeMillis()
        return end - startedAt
    }
}
