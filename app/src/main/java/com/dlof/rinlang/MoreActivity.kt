package com.dlof.rinlang

import android.content.Intent
import android.os.Bundle
import android.view.View
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.dlof.rinlang.store.extensions.RinExtensionsMarketplaceActivity

/**
 * شاشة "المزيد": تجمع باقي أقسام التطبيق التي لا تظهر مباشرة في الشريط السفلي،
 * مرتّبة كما يلي: الملفات، المكتبات، منفّذ الأنابيب (RinFlow)، متجر الإضافات، الإعدادات.
 *
 * الملفات والمكتبات مرتبطتان بمشروع محدد (EXTRA_PROJECT_NAME)، لذا عند عدم وجود
 * مشروع مفتوح حالياً يتم توجيه المستخدم إلى شاشة المشاريع أولاً لاختيار واحد.
 */
class MoreActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_more)
        BottomNavHelper.setup(this, BottomNavTab.MORE)

        findViewById<TextView>(R.id.txtToolbarTitle).text = getString(R.string.more_screen_title)
        findViewById<View>(R.id.btnToolbarBack).setOnClickListener { finish() }

        // 1. الملفات
        findViewById<View>(R.id.rowFiles).setOnClickListener {
            openProjectScoped(FilesActivity::class.java, FilesActivity.EXTRA_PROJECT_NAME)
        }

        // 2. المكتبات
        findViewById<View>(R.id.rowLibraries).setOnClickListener {
            openProjectScoped(LibrariesActivity::class.java, LibrariesActivity.EXTRA_PROJECT_NAME)
        }

        // 3. منفّذ الأنابيب (RinFlow) — لا يحتاج مشروعاً محدداً
        findViewById<View>(R.id.rowPipeline).setOnClickListener {
            startActivity(Intent(this, PipelineRunnerActivity::class.java))
        }

        // 4. متجر الإضافات
        findViewById<View>(R.id.rowExtensions).setOnClickListener {
            startActivity(Intent(this, RinExtensionsMarketplaceActivity::class.java))
        }

        // 5. الإعدادات
        findViewById<View>(R.id.rowSettings).setOnClickListener {
            startActivity(Intent(this, SettingsActivity::class.java))
        }
    }

    /**
     * الملفات والمكتبات شاشتان مرتبطتان بمشروع واحد محدد. إن كان هناك مشروع واحد
     * فقط نفتحه مباشرة توفيراً لخطوة إضافية، وإن تعدّدت المشاريع أو انعدمت نوجّه
     * المستخدم إلى شاشة المشاريع ليختار (أو لينشئ مشروعاً جديداً).
     */
    private fun openProjectScoped(target: Class<*>, extraKey: String) {
        val projects = ProjectManager.listProjects(this)
        if (projects.size == 1) {
            startActivity(Intent(this, target).putExtra(extraKey, projects[0].name))
        } else {
            Toast.makeText(this, getString(R.string.more_row_needs_project_toast), Toast.LENGTH_SHORT).show()
            startActivity(Intent(this, ProjectsActivity::class.java))
        }
    }
}
