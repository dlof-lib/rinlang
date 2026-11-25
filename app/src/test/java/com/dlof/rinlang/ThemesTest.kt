package com.dlof.rinlang

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** سلامة ثيمات المحرر وسمات الطرفية (بما فيها الإضافية) — بلا واجهة. */
class ThemesTest {

    private val newEditorIds = listOf(
        "rinstudio_black", "rinstudio_light", "jellyfish", "tokyo_night",
        "catppuccin_mocha", "gruvbox_dark", "catppuccin_latte", "high_contrast"
    )

    @Test fun editorPresets_haveUniqueIds() {
        val ids = EditorTheme.PRESETS.map { it.id }
        assertEquals(ids.size, ids.toSet().size)
    }

    @Test fun editorPresets_defineEveryGroupAndSurface() {
        val keys = EditorTheme.GROUPS.map { it.key } + EditorTheme.SURFACES
        for (p in EditorTheme.PRESETS) {
            for (k in keys) assertTrue("${p.id} ينقصه $k", p.colors.containsKey(k))
        }
    }

    @Test fun newEditorPresets_existAndTextIsReadable() {
        for (id in newEditorIds) {
            val p = EditorTheme.preset(id)
            assertNotNull("غير موجود: $id", p)
            val bg = p!!.colors.getValue(EditorTheme.SURFACE_BG)
            val text = p.colors.getValue(EditorTheme.SURFACE_TEXT)
            assertTrue("$id تباين النص ضعيف", RinTheme.contrast(text, bg) >= 4.5)
            // علم dark يطابق سطوع الخلفية الفعلي
            assertEquals("$id", p.dark, !(RinTheme.luminance(bg) > RinTheme.LIGHT_THRESHOLD))
        }
    }

    @Test fun terminalThemes_uniqueIdsAndLookup() {
        val ids = RinTheme.ALL.map { it.id }
        assertEquals(ids.size, ids.toSet().size)
        for (t in RinTheme.ALL) assertEquals(t.id, RinTheme.byId(t.id).id)
        assertEquals("rinstudio", RinTheme.find("rinstudio")?.id)
        assertEquals("latte", RinTheme.find("latte")?.id)
    }

    @Test fun terminalThemes_lightFlagMatchesBackground() {
        for (id in listOf("rinlight", "latte")) assertTrue(id, RinTheme.byId(id).isLight)
        for (id in listOf("rinstudio", "jellyfish", "tokyo", "mocha", "gruvbox")) assertFalse(id, RinTheme.byId(id).isLight)
        assertTrue(RinTheme.LIGHT.any { it.id == "rinlight" })
        assertTrue(RinTheme.DARK.any { it.id == "rinstudio" })
    }

    @Test fun terminalThemes_foregroundReadableOnBackground() {
        for (id in listOf("rinstudio", "jellyfish", "tokyo", "mocha", "gruvbox", "rinlight", "latte")) {
            val t = RinTheme.byId(id)
            assertTrue("$id", RinTheme.contrast(t.fg, t.bg) >= 4.5)
        }
    }
}
