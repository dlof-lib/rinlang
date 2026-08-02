package com.dlof.rinlang

import android.graphics.BitmapFactory
import android.media.MediaController
import android.net.Uri
import android.os.Bundle
import android.view.View
import android.widget.ImageView
import android.widget.TextView
import android.widget.Toast
import android.widget.VideoView
import androidx.appcompat.app.AppCompatActivity
import java.io.File

/**
 * معاينة بسيطة داخل التطبيق لملف صورة أو فيديو مرفوع في مشروع (بدل فتح ملف .rin في المحرر
 * النصّي، أو الاعتماد على تطبيق خارجي لكل صورة/فيديو). تُفتَح من [FilesActivity] عند الضغط على
 * أي ملف يحدّده [ProjectManager.isImageFile] أو [ProjectManager.isVideoFile].
 */
class MediaPreviewActivity : AppCompatActivity() {

    companion object {
        const val EXTRA_FILE_PATH = "extra_file_path"
        const val EXTRA_FILE_NAME = "extra_file_name"
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_media_preview)

        val path = intent.getStringExtra(EXTRA_FILE_PATH)
        val fileName = intent.getStringExtra(EXTRA_FILE_NAME) ?: getString(R.string.files_screen_title)

        findViewById<TextView>(R.id.txtToolbarTitle).text = fileName
        findViewById<View>(R.id.btnToolbarBack).setOnClickListener { finish() }

        val imgPreview: ImageView = findViewById(R.id.imgMediaPreview)
        val videoPreview: VideoView = findViewById(R.id.videoMediaPreview)
        val txtError: TextView = findViewById(R.id.txtMediaPreviewError)

        val file = path?.let { File(it) }
        if (file == null || !file.exists()) {
            txtError.visibility = View.VISIBLE
            return
        }

        when {
            ProjectManager.isVideoFile(fileName) -> {
                videoPreview.visibility = View.VISIBLE
                try {
                    val controller = MediaController(this)
                    controller.setAnchorView(videoPreview)
                    videoPreview.setMediaController(controller)
                    videoPreview.setVideoURI(Uri.fromFile(file))
                    videoPreview.setOnPreparedListener { it.isLooping = false }
                    videoPreview.setOnErrorListener { _, _, _ ->
                        videoPreview.visibility = View.GONE
                        txtError.visibility = View.VISIBLE
                        true
                    }
                    videoPreview.start()
                } catch (t: Throwable) {
                    videoPreview.visibility = View.GONE
                    txtError.visibility = View.VISIBLE
                }
            }
            else -> {
                imgPreview.visibility = View.VISIBLE
                val bitmap = try {
                    BitmapFactory.decodeFile(file.absolutePath)
                } catch (t: Throwable) {
                    null
                }
                if (bitmap != null) {
                    imgPreview.setImageBitmap(bitmap)
                } else {
                    imgPreview.visibility = View.GONE
                    txtError.visibility = View.VISIBLE
                    Toast.makeText(this, R.string.file_open_error, Toast.LENGTH_SHORT).show()
                }
            }
        }
    }

    override fun onPause() {
        super.onPause()
        // إيقاف تشغيل الفيديو عند مغادرة الشاشة (تدوير الجهاز، الانتقال لتطبيق آخر...).
        findViewById<VideoView>(R.id.videoMediaPreview)?.let {
            if (it.isPlaying) it.pause()
        }
    }
}
