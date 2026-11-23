package com.dlof.rinlang

/** يختار أيقونة المجلد (rs_folder*) حسب اسمه وحالته (مفتوح/مغلق/فارغ). */
object RsFolderIcons {
    fun forName(name: String, open: Boolean = false, empty: Boolean = false): Int {
        if (empty) return R.drawable.rs_folder_empty
        return when (name.lowercase()) {
            "src", "source", "sources", "app", "scripts" -> R.drawable.rs_folder_src
            "lib", "libs", "packages", "vendor", "node_modules", "deps" -> R.drawable.rs_folder_lib
            "assets", "images", "img", "media", "res", "static", "public" -> R.drawable.rs_folder_assets
            "docs", "doc", "documentation" -> R.drawable.rs_folder_docs
            "test", "tests", "spec", "specs", "__tests__" -> R.drawable.rs_folder_tests
            "config", "configs", "settings", "conf", ".github", ".vscode" -> R.drawable.rs_folder_config
            else -> if (open) R.drawable.rs_folder_open else R.drawable.rs_folder
        }
    }
}
