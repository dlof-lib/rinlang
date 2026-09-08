package com.dlof.rinlang

import android.app.AlertDialog
import android.graphics.Color
import android.graphics.drawable.GradientDrawable
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.EditText
import android.widget.ImageButton
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.recyclerview.widget.GridLayoutManager
import androidx.recyclerview.widget.RecyclerView

/** Dedicated, visual album browser. Albums are presented as physical-style covers. */
class AlbumsActivity : AppCompatActivity() {
    private lateinit var adapter: AlbumsAdapter
    private lateinit var empty: View

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_albums)
        BottomNavHelper.setup(this, BottomNavTab.PROJECTS)

        findViewById<TextView>(R.id.txtAlbumToolbarTitle).text = getString(R.string.albums_title)
        findViewById<View>(R.id.btnAlbumBack).setOnClickListener { finish() }
        findViewById<View>(R.id.btnNewAlbum).setOnClickListener { showCreateAlbumDialog() }

        empty = findViewById(R.id.albumEmptyState)
        adapter = AlbumsAdapter(
            onOpen = { openAlbum(it) },
            onDelete = { deleteAlbum(it) }
        )
        findViewById<RecyclerView>(R.id.rvAlbums).apply {
            layoutManager = GridLayoutManager(this@AlbumsActivity, 2)
            adapter = this@AlbumsActivity.adapter
            setHasFixedSize(false)
        }
    }

    override fun onResume() {
        super.onResume()
        refresh()
    }

    private fun refresh() {
        val albums = ProjectAlbumManager.listAlbums(this)
        adapter.submit(albums)
        empty.visibility = if (albums.isEmpty()) View.VISIBLE else View.GONE
        findViewById<TextView>(R.id.txtAlbumCount).text = getString(R.string.album_count_format, albums.size)
    }

    private fun openAlbum(name: String) {
        startActivity(android.content.Intent(this, AlbumDetailActivity::class.java).apply {
            putExtra(AlbumDetailActivity.EXTRA_ALBUM_NAME, name)
        })
    }

    private fun deleteAlbum(name: String) {
        AlertDialog.Builder(this)
            .setTitle(R.string.album_delete_title)
            .setMessage(getString(R.string.album_delete_confirm, name))
            .setPositiveButton(R.string.delete) { _, _ ->
                ProjectAlbumManager.deleteAlbum(this, name)
                refresh()
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun showCreateAlbumDialog() {
        val input = EditText(this).apply {
            hint = getString(R.string.album_name_hint)
            setSingleLine(true)
            setTextColor(ContextCompat.getColor(this@AlbumsActivity, R.color.rin_on_toolbar))
            setHintTextColor(ContextCompat.getColor(this@AlbumsActivity, R.color.rin_editor_hint))
            background = ContextCompat.getDrawable(this@AlbumsActivity, R.drawable.bg_input_field)
            val pad = (14 * resources.displayMetrics.density).toInt()
            setPadding(pad, pad, pad, pad)
        }
        val container = android.widget.FrameLayout(this).apply {
            val margin = (22 * resources.displayMetrics.density).toInt()
            addView(input, android.widget.FrameLayout.LayoutParams(
                android.widget.FrameLayout.LayoutParams.MATCH_PARENT,
                android.widget.FrameLayout.LayoutParams.WRAP_CONTENT
            ).apply { marginStart = margin; marginEnd = margin; topMargin = margin / 2 })
        }
        AlertDialog.Builder(this)
            .setTitle(R.string.album_new)
            .setMessage(R.string.album_create_description)
            .setView(container)
            .setPositiveButton(R.string.create) { _, _ ->
                try {
                    ProjectAlbumManager.createAlbum(this, input.text.toString())
                    refresh()
                    Toast.makeText(this, R.string.album_created, Toast.LENGTH_SHORT).show()
                } catch (e: IllegalArgumentException) {
                    Toast.makeText(this, e.message, Toast.LENGTH_SHORT).show()
                }
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }
}

private class AlbumsAdapter(
    private val onOpen: (String) -> Unit,
    private val onDelete: (String) -> Unit
) : RecyclerView.Adapter<AlbumsAdapter.VH>() {
    private var items: List<String> = emptyList()

    fun submit(value: List<String>) {
        items = value
        notifyDataSetChanged()
    }

    class VH(view: View) : RecyclerView.ViewHolder(view) {
        val cover: View = view.findViewById(R.id.albumCover)
        val coverBack: View = view.findViewById(R.id.albumCoverBack)
        val title: TextView = view.findViewById(R.id.txtAlbumName)
        val meta: TextView = view.findViewById(R.id.txtAlbumMeta)
        val delete: ImageButton = view.findViewById(R.id.btnDeleteAlbum)
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): VH =
        VH(LayoutInflater.from(parent.context).inflate(R.layout.item_album, parent, false))

    override fun onBindViewHolder(holder: VH, position: Int) {
        val name = items[position]
        val context = holder.itemView.context
        val projects = ProjectAlbumManager.projectsInAlbum(context, name)
        val palette = listOf("#7C5CFF", "#22C88E", "#3B82F6", "#F59E0B", "#EC4899", "#06B6D4")
        val color = Color.parseColor(palette[position % palette.size])

        holder.cover.background = GradientDrawable(GradientDrawable.Orientation.TL_BR, intArrayOf(color, darken(color, .55f))).apply {
            cornerRadius = 24f * context.resources.displayMetrics.density
        }
        holder.coverBack.background = GradientDrawable().apply {
            shape = GradientDrawable.RECTANGLE
            setColor(darken(color, .72f))
            cornerRadius = 22f * context.resources.displayMetrics.density
        }
        holder.title.text = name
        holder.meta.text = context.getString(R.string.album_projects_count_format, projects.size)
        holder.itemView.setOnClickListener { onOpen(name) }
        holder.delete.setOnClickListener { onDelete(name) }
    }

    override fun getItemCount() = items.size

    private fun darken(c: Int, amount: Float): Int {
        val r = (Color.red(c) * amount).toInt()
        val g = (Color.green(c) * amount).toInt()
        val b = (Color.blue(c) * amount).toInt()
        return Color.rgb(r, g, b)
    }
}
