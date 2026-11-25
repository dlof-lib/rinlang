package com.dlof.rinlang

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** دوال التسهيل المشتركة بين شاشة البداية ومؤشرات التحميل — بلا واجهة. */
class RinEaseTest {

    @Test fun clamp01_boundsValues() {
        assertEquals(0f, RinEase.clamp01(-3f), 0f)
        assertEquals(1f, RinEase.clamp01(7f), 0f)
        assertEquals(0.4f, RinEase.clamp01(0.4f), 0f)
    }

    @Test fun inOutAndOut_haveFixedEndpoints() {
        assertEquals(0f, RinEase.inOut(0f), 1e-6f)
        assertEquals(1f, RinEase.inOut(1f), 1e-6f)
        assertEquals(0f, RinEase.out(0f), 1e-6f)
        assertEquals(1f, RinEase.out(1f), 1e-6f)
    }

    @Test fun swimPulse_peaksAfterQuickContractionThenRelaxes() {
        assertEquals(0f, RinEase.swimPulse(0f), 1e-6f)
        assertEquals(1f, RinEase.swimPulse(0.28f), 1e-4f)
        assertTrue(RinEase.swimPulse(0.6f) < RinEase.swimPulse(0.28f))
        // دورية: p و p+1 يعطيان القيمة نفسها
        assertEquals(RinEase.swimPulse(0.4f), RinEase.swimPulse(3.4f), 1e-4f)
    }
}
