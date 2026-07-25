package com.dlof.rinlang

import android.content.Context
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import androidx.core.content.ContextCompat
import androidx.recyclerview.widget.RecyclerView

class RinJobAdapter(private val context: Context) : RecyclerView.Adapter<RinJobAdapter.JobViewHolder>() {

    private var items: List<RinJob> = emptyList()

    fun submit(newItems: List<RinJob>) {
        items = newItems
        notifyDataSetChanged()
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): JobViewHolder {
        val view = LayoutInflater.from(parent.context).inflate(R.layout.item_rin_job, parent, false)
        return JobViewHolder(view)
    }

    override fun onBindViewHolder(holder: JobViewHolder, position: Int) {
        holder.bind(items[position])
    }

    override fun getItemCount(): Int = items.size

    inner class JobViewHolder(itemView: View) : RecyclerView.ViewHolder(itemView) {
        private val dot: View = itemView.findViewById(R.id.viewStatusDot)
        private val title: TextView = itemView.findViewById(R.id.txtJobTitle)
        private val statusText: TextView = itemView.findViewById(R.id.txtJobStatus)
        private val duration: TextView = itemView.findViewById(R.id.txtJobDuration)
        private val output: TextView = itemView.findViewById(R.id.txtJobOutput)

        fun bind(job: RinJob) {
            title.text = context.getString(R.string.job_title_fmt, job.number)

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

            output.text = when (job.status) {
                JobStatus.QUEUED -> ""
                JobStatus.RUNNING -> "…"
                else -> job.output
            }
        }
    }
}
