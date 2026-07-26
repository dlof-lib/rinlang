package com.dlof.rinlang

import android.Manifest
import android.app.Activity
import android.content.Context
import android.content.pm.PackageManager
import android.graphics.Typeface
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.core.app.ActivityCompat
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
        private val fallbackOutput: TextView = itemView.findViewById(R.id.txtJobOutput)
        private val outputLines: LinearLayout = itemView.findViewById(R.id.llJobOutputLines)
        private val artifactsContainer: LinearLayout = itemView.findViewById(R.id.llJobArtifacts)
        private val dp = itemView.resources.displayMetrics.density

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

            outputLines.removeAllViews()
            artifactsContainer.removeAllViews()
            artifactsContainer.visibility = View.GONE

            when (job.status) {
                JobStatus.QUEUED -> {
                    fallbackOutput.visibility = View.GONE
                    fallbackOutput.text = ""
                }
                JobStatus.RUNNING -> {
                    fallbackOutput.visibility = View.VISIBLE
                    fallbackOutput.text = "…"
                }
                else -> {
                    fallbackOutput.visibility = View.GONE
                    fallbackOutput.text = ""
                    renderOutput(job)
                }
            }
        }

        /** Builds one icon + styled-text row per output line, and download chips for real artifacts. */
        private fun renderOutput(job: RinJob) {
            val lines = RinConsoleFormatter.formatLines(job.output)
            for (line in lines) {
                outputLines.addView(buildLineRow(line))
            }

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

        private fun buildLineRow(line: RinLogLine): View {
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
                text = line.text
                textSize = 12.5f
                typeface = Typeface.MONOSPACE
                setTextColor(if (line.kind == LogKind.PLAIN) ContextCompat.getColor(context, R.color.rin_console_text) else tint)
                setTextIsSelectable(true)
                layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
            }
            row.addView(text)
            return row
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
