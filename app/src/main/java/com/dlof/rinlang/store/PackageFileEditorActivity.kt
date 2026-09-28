package com.dlof.rinlang.store

import android.app.AlertDialog
import android.graphics.BitmapFactory
import android.os.Bundle
import android.util.TypedValue
import android.view.View
import android.widget.Button
import android.widget.ImageButton
import android.widget.ImageView
import android.widget.TextView
import android.widget.Toast
import androidx.activity.OnBackPressedCallback
import com.dlof.rinlang.AppSettings
import com.dlof.rinlang.R
import com.dlof.rinlang.RinCodeEditorView
import com.dlof.rinlang.network.BaseConnectivityActivity

/**
 * محرر ملف واحد داخل مساحة عمل تعديل الحزمة ([PackageWorkspace]) — يستخدم محرر الكود الأصلي
 * [RinCodeEditorView] (نفس محرك C++ والتلوين النحوي) بدل EditText عادي. الملفات الصورية تُعرض
 * كمعاينة، والملفات الثنائية/الكبيرة تُعرض برسالة بدل محاولة فتحها. الحفظ محلي في مساحة العمل؛
 * النشر الفعلي للمتجر من شاشة [EditPackageActivity].
 */
class PackageFileEditorActivity : BaseConnectivityActivity() {

    companion object {
        const val EXTRA_PACKAGE_ID = "extra_editor_package_id"
        const val EXTRA_REL_PATH = "extra_editor_rel_path"
    }

    private var workspace: PackageWorkspace? = null
    private lateinit var relPath: String
    private var editor: RinCodeEditorView? = null
    private var savedSnapshot: String = ""
    private var editable = false
    private var savedOnce = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_package_file_editor)

        val packageId = intent.getStringExtra(EXTRA_PACKAGE_ID)
        val path = intent.getStringExtra(EXTRA_REL_PATH)
        val ws = packageId?.let { PackageWorkspace.openExisting(applicationContext, it) }
        if (path == null || ws == null || !ws.exists(path)) {
            Toast.makeText(this, R.string.edit_package_open_failed, Toast.LENGTH_LONG).show()
            finish()
            return
        }
        workspace = ws
        relPath = path

        val fileName = path.substringAfterLast('/')
        findViewById<TextView>(R.id.txtToolbarTitle).text = fileName
        findViewById<TextView>(R.id.txtToolbarSubtitle).apply {
            text = path
            visibility = View.VISIBLE
        }
        findViewById<View>(R.id.btnToolbarBack).setOnClickListener { onBackPressedDispatcher.onBackPressed() }

        onBackPressedDispatcher.addCallback(this, object : OnBackPressedCallback(true) {
            override fun handleOnBackPressed() {
                if (hasUnsaved()) confirmUnsaved() else finishWithResult()
            }
        })

        val actions = findViewById<View>(R.id.rowFileEditorActions)
        val scroll = findViewById<View>(R.id.scrollFileEditor)
        val img = findViewById<ImageView>(R.id.imgFilePreview)
        val note = findViewById<TextView>(R.id.txtFileNotEditable)

        when {
            PackageWorkspace.isImage(fileName) -> {
                actions.visibility = View.GONE
                scroll.visibility = View.GONE
                previewImage(ws, path, img, note)
            }
            !ws.isEditableText(path) -> {
                actions.visibility = View.GONE
                scroll.visibility = View.GONE
                note.text = getString(R.string.edit_package_not_editable)
                note.visibility = View.VISIBLE
            }
            else -> setupEditor(ws, path, fileName)
        }
    }

    private fun setupEditor(ws: PackageWorkspace, path: String, fileName: String) {
        editable = true
        val view = findViewById<RinCodeEditorView>(R.id.editPackageFile)
        editor = view

        view.setTextSize(TypedValue.COMPLEX_UNIT_SP, AppSettings.getEditorFontSizeSp(this))
        view.setLanguage(fileName.substringAfterLast('.', ""))
        val content = ws.readText(path)
        view.setText(content)
        savedSnapshot = view.text.toString()

        findViewById<ImageButton>(R.id.btnFileEditorUndo).setOnClickListener {
            if (!view.hasFocus()) view.requestFocus()
            view.undo()
        }
        findViewById<ImageButton>(R.id.btnFileEditorRedo).setOnClickListener {
            if (!view.hasFocus()) view.requestFocus()
            view.redo()
        }
        findViewById<Button>(R.id.btnFileEditorSave).setOnClickListener { save() }

        val status = findViewById<TextView>(R.id.txtFileEditorStatus)
        fun refreshStatus() {
            val info = view.statusInfo()
            status.text = (if (hasUnsaved()) "● " else "") + "${info.line}:${info.col}  ·  ${view.lineCount()}"
        }
        view.addCursorStateChangeListener { refreshStatus() }
        view.addVisibleLinesChangeListener { refreshStatus() }
        refreshStatus()
    }

    private fun previewImage(ws: PackageWorkspace, path: String, img: ImageView, note: TextView) {
        try {
            val file = ws.resolve(path)
            val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
            BitmapFactory.decodeFile(file.absolutePath, bounds)
            var sample = 1
            while (bounds.outWidth / sample > 2048 || bounds.outHeight / sample > 2048) sample *= 2
            val bmp = BitmapFactory.decodeFile(file.absolutePath, BitmapFactory.Options().apply { inSampleSize = sample })
            if (bmp != null) {
                img.setImageBitmap(bmp)
                img.visibility = View.VISIBLE
                return
            }
        } catch (t: Throwable) { /* ينزل إلى الرسالة أدناه */ }
        note.text = getString(R.string.edit_package_not_editable)
        note.visibility = View.VISIBLE
    }

    private fun hasUnsaved(): Boolean = editable && editor?.text?.toString() != savedSnapshot

    private fun save(): Boolean {
        val ws = workspace ?: return false
        val view = editor ?: return false
        return try {
            val text = view.text.toString()
            ws.writeText(relPath, text)
            savedSnapshot = text
            savedOnce = true
            Toast.makeText(this, R.string.edit_package_file_saved, Toast.LENGTH_SHORT).show()
            true
        } catch (t: Throwable) {
            Toast.makeText(this, t.message ?: getString(R.string.edit_package_save_failed), Toast.LENGTH_LONG).show()
            false
        }
    }

    private fun confirmUnsaved() {
        AlertDialog.Builder(this)
            .setTitle(R.string.edit_package_file_unsaved_title)
            .setMessage(R.string.edit_package_file_unsaved_message)
            .setPositiveButton(R.string.action_save) { _, _ -> if (save()) finishWithResult() }
            .setNegativeButton(R.string.edit_package_file_discard) { _, _ -> finishWithResult() }
            .setNeutralButton(R.string.cancel, null)
            .show()
    }

    /** يُبلغ شاشة التعديل (RESULT_OK) إن حُفظ أي تغيير، لتعلّم ملفات الحزمة كمعدَّلة وتُعيد رسم القائمة. */
    private fun finishWithResult() {
        if (savedOnce) setResult(RESULT_OK)
        finish()
    }
}
