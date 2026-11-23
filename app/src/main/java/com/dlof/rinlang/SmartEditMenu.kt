package com.dlof.rinlang

import android.app.AlertDialog
import android.content.Context
import android.widget.Toast

/**
 * قائمة «الكتابة الذكية» في قائمة التحرير: كل أوامر [SmartEdit] ومعاينة الألوان في مكان واحد منظَّم
 * (حالة الأحرف، أرقام وقيم، إحاطة، تحديد، أسطر، ألوان). تفتح من MainActivity (قائمة Edit).
 */
object SmartEditMenu {

    private class Item(val title: String, val run: (RinCodeEditorView) -> Boolean)

    fun show(context: Context, editor: RinCodeEditorView) {
        val s = { id: Int -> context.getString(id) }
        val groups = LinkedHashMap<String, List<Item>>()
        groups[s(R.string.smart_group_case)] = listOf(
            Item(s(R.string.smart_case_upper)) { it.smartChangeCase(SmartEdit.CaseMode.UPPER) },
            Item(s(R.string.smart_case_lower)) { it.smartChangeCase(SmartEdit.CaseMode.LOWER) },
            Item(s(R.string.smart_case_title)) { it.smartChangeCase(SmartEdit.CaseMode.TITLE) },
            Item("snake_case") { it.smartChangeCase(SmartEdit.CaseMode.SNAKE) },
            Item("kebab-case") { it.smartChangeCase(SmartEdit.CaseMode.KEBAB) },
            Item("camelCase") { it.smartChangeCase(SmartEdit.CaseMode.CAMEL) },
            Item("PascalCase") { it.smartChangeCase(SmartEdit.CaseMode.PASCAL) },
            Item("CONSTANT_CASE") { it.smartChangeCase(SmartEdit.CaseMode.CONSTANT) }
        )
        groups[s(R.string.smart_group_values)] = listOf(
            Item(s(R.string.smart_number_inc)) { it.smartStepNumber(1) },
            Item(s(R.string.smart_number_dec)) { it.smartStepNumber(-1) },
            Item(s(R.string.action_flip_word)) { it.smartFlipWord() }
        )
        groups[s(R.string.smart_group_wrap)] = listOf(
            Item("( … )") { it.smartSurround('(') },
            Item("[ … ]") { it.smartSurround('[') },
            Item("{ … }") { it.smartSurround('{') },
            Item("\" … \"") { it.smartSurround('"') },
            Item("' … '") { it.smartSurround('\'') },
            Item(s(R.string.smart_unwrap)) { it.smartUnsurround() }
        )
        groups[s(R.string.smart_group_select)] = listOf(
            Item(s(R.string.action_expand_selection)) { it.smartExpandSelection() },
            Item(s(R.string.smart_line_below)) { it.smartInsertLineBelow() },
            Item(s(R.string.smart_line_above)) { it.smartInsertLineAbove() }
        )
        groups[s(R.string.smart_group_lines)] = listOf(
            Item(s(R.string.smart_lines_reverse)) { it.smartLineOp(SmartEdit.LineOp.REVERSE) },
            Item(s(R.string.smart_lines_unique)) { it.smartLineOp(SmartEdit.LineOp.UNIQUE) },
            Item(s(R.string.smart_lines_sort_numeric)) { it.smartLineOp(SmartEdit.LineOp.SORT_NUMERIC) }
        )
        groups[s(R.string.smart_group_colors)] = listOf(
            Item(s(R.string.action_edit_color)) { it.pickColorAtCursor() },
            Item(s(R.string.action_cycle_color_format)) { it.smartCycleColor() }
        )

        // قائمة مسطّحة مع عناوين المجموعات كصفوف غير قابلة للاختيار (تُتخطّى عند الضغط)
        val labels = ArrayList<String>(); val actions = ArrayList<Item?>()
        for ((g, items) in groups) {
            labels.add("── $g ──"); actions.add(null)
            for (i in items) { labels.add(i.title); actions.add(i) }
        }
        // وضع معاينة الألوان + الإحاطة عند الكتابة كخيارين تبديل في آخر القائمة
        val modeNames = arrayOf(s(R.string.color_preview_off), s(R.string.color_preview_box), s(R.string.color_preview_chip))
        labels.add("── ${s(R.string.smart_group_settings)} ──"); actions.add(null)
        labels.add(s(R.string.color_preview_mode) + ": " + modeNames[editor.getColorPreviewMode()])
        actions.add(Item("") { v -> v.setColorPreviewMode((v.getColorPreviewMode() + 1) % 3); true })
        val surroundOn = EditorDesign.isSurroundOnType(context)
        labels.add(s(R.string.smart_surround_on_type) + ": " + s(if (surroundOn) R.string.smart_on else R.string.smart_off))
        actions.add(Item("") { _ -> EditorDesign.setSurroundOnType(context, !surroundOn); true })

        AlertDialog.Builder(context)
            .setTitle(R.string.menu_edit_smart)
            .setItems(labels.toTypedArray()) { dialog, which ->
                val item = actions[which] ?: return@setItems
                dialog.dismiss()
                val ok = item.run(editor)
                if (!ok) Toast.makeText(context, R.string.no_changes_toast, Toast.LENGTH_SHORT).show()
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }
}
