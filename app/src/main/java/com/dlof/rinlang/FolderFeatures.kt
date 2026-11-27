package com.dlof.rinlang

import android.app.AlertDialog
import android.app.Activity
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.graphics.Color
import android.graphics.drawable.GradientDrawable
import android.view.Gravity
import android.view.View
import android.widget.LinearLayout
import android.widget.Toast

/*
 * مزايا المجلدات:
 *   • FolderColors : لون مخصّص لكل مجلد (يُحفظ لكل مشروع) يلوّن بلاطة الأيقونة وشارة العدد.
 *   • FolderMenu   : قائمة إجراءات سريعة للمجلد (لون، تثبيت، نسخ المسار، خصائص، ضغط) + "كل الإجراءات".
 */
object FolderColors {
    private const val PREFS = "rin_folder_colors"

    val PALETTE: IntArray = intArrayOf(
        0xFF3DF59A.toInt(), 0xFFA78BFA.toInt(), 0xFFFFB020.toInt(), 0xFF22D3EE.toInt(),
        0xFF60A5FA.toInt(), 0xFFF472B6.toInt(), 0xFFFB7185.toInt(), 0xFF94A3B8.toInt()
    )

    private fun key(project: String, rel: String) = "$project|$rel"

    fun get(ctx: Context, project: String, rel: String): Int? {
        val p = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        val k = key(project, rel)
        return if (p.contains(k)) p.getInt(k, 0) else null
    }

    fun set(ctx: Context, project: String, rel: String, color: Int?) {
        val e = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
        if (color == null) e.remove(key(project, rel)) else e.putInt(key(project, rel), color)
        e.apply()
    }

    /** خلفية بلاطة ملوّنة: تعبئة شفافة بلون المجلد وحدّ أوضح بزوايا 16dp. */
    fun tileBackground(ctx: Context, color: Int): GradientDrawable {
        val d = ctx.resources.displayMetrics.density
        return GradientDrawable().apply {
            shape = GradientDrawable.RECTANGLE
            cornerRadius = 16 * d
            setColor(Color.argb(0x2E, Color.red(color), Color.green(color), Color.blue(color)))
            setStroke((1.5f * d).toInt(), Color.argb(0x99, Color.red(color), Color.green(color), Color.blue(color)))
        }
    }
}

object FolderMenu {
    fun show(
        activity: Activity,
        project: Project,
        entry: FsEntry.FolderEntry,
        parentRelDir: String,
        onChanged: () -> Unit,
        showAll: () -> Unit
    ) {
        val labels = arrayOf(
            activity.getString(R.string.fm_color),
            activity.getString(R.string.fm_pin),
            activity.getString(R.string.fm_copy_path),
            activity.getString(R.string.fm_props),
            activity.getString(R.string.fm_zip),
            activity.getString(R.string.fm_tree),
            activity.getString(R.string.fm_all)
        )
        AlertDialog.Builder(activity)
            .setTitle(entry.name)
            .setItems(labels) { _, which ->
                when (which) {
                    0 -> pickColor(activity, project, entry, onChanged)
                    1 -> { FilePins.toggle(activity, project.name, listOf(entry)); onChanged() }
                    2 -> {
                        val cm = activity.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
                        cm.setPrimaryClip(ClipData.newPlainText("path", entry.relPath))
                        Toast.makeText(activity, R.string.path_copied_toast, Toast.LENGTH_SHORT).show()
                    }
                    3 -> FileProps.show(activity, listOf(entry))
                    4 -> FileArchive.compress(activity, project, listOf(entry), parentRelDir, onChanged)
                    5 -> {
                        val tree = BatchTemplate.fromDir(entry.folder.dir)
                        val cm = activity.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
                        cm.setPrimaryClip(ClipData.newPlainText("tree", tree))
                        Toast.makeText(activity, activity.getString(R.string.fm_tree_done, if (tree.isEmpty()) 0 else tree.lines().size), Toast.LENGTH_LONG).show()
                    }
                    6 -> showAll()
                }
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun pickColor(activity: Activity, project: Project, entry: FsEntry.FolderEntry, onChanged: () -> Unit) {
        val d = activity.resources.displayMetrics.density
        val holder = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            setPadding((20 * d).toInt(), (12 * d).toInt(), (20 * d).toInt(), (4 * d).toInt())
        }
        lateinit var dialog: AlertDialog
        val size = (44 * d).toInt()
        val gap = (6 * d).toInt()
        FolderColors.PALETTE.toList().chunked(4).forEach { rowColors ->
            val row = LinearLayout(activity).apply { gravity = Gravity.CENTER; orientation = LinearLayout.HORIZONTAL }
            rowColors.forEach { c ->
                val dot = View(activity).apply {
                    background = GradientDrawable().apply { shape = GradientDrawable.OVAL; setColor(c) }
                    layoutParams = LinearLayout.LayoutParams(size, size).apply { setMargins(gap, gap, gap, gap) }
                    setOnClickListener {
                        FolderColors.set(activity, project.name, entry.relPath, c)
                        dialog.dismiss(); onChanged()
                    }
                }
                row.addView(dot)
            }
            holder.addView(row)
        }
        dialog = AlertDialog.Builder(activity)
            .setTitle(R.string.fm_color)
            .setView(holder)
            .setNeutralButton(R.string.fm_color_default) { _, _ ->
                FolderColors.set(activity, project.name, entry.relPath, null); onChanged()
            }
            .setNegativeButton(R.string.cancel, null)
            .create()
        dialog.show()
    }
}
