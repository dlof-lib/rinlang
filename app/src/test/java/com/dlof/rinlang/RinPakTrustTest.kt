package com.dlof.rinlang

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** منطق ثقة الناشرين لحزم RinPak ([RinPakTrust]) — بلا Android. */
class RinPakTrustTest {

    private class MemStore : RinPakTrust.Store {
        val m = HashMap<String, String>()
        override fun get(key: String) = m[key]
        override fun put(key: String, value: String) { m[key] = value }
        override fun remove(key: String) { m.remove(key) }
    }

    private val k1 = "aaaaaaaaaaaaaaaa"
    private val k2 = "bbbbbbbbbbbbbbbb"
    private val own = "cccccccccccccccc"

    @Test fun unsignedAndOwn() {
        val t = RinPakTrust(MemStore())
        assertEquals(RinPakTrust.Status.UNSIGNED, t.assess("App", null, own).status)
        assertEquals(RinPakTrust.Status.OWN, t.assess("App", own.uppercase(), own).status)
    }

    @Test fun newThenKnownSigner() {
        val t = RinPakTrust(MemStore())
        val first = t.assess("App", k1, own)
        assertEquals(RinPakTrust.Status.NEW_SIGNER, first.status)
        assertTrue(first.canTrust)
        t.recordImport("App", k1)
        assertEquals(RinPakTrust.Status.KNOWN_SIGNER, t.assess(" app ", k1, own).status)
    }

    @Test fun keyChangeAndStrippedSignatureWarn() {
        val t = RinPakTrust(MemStore())
        t.recordImport("App", k1)
        val changed = t.assess("App", k2, own)
        assertEquals(RinPakTrust.Status.KEY_CHANGED, changed.status)
        assertEquals(k1, changed.previousKeyId)
        assertTrue(changed.isWarning)
        assertTrue(t.assess("App", null, own).isWarning)
    }

    @Test fun explicitTrustOverridesAndCanBeRevoked() {
        val t = RinPakTrust(MemStore())
        t.recordImport("App", k1)
        t.trust(k2, "Droy")
        val a = t.assess("App", k2, own)
        assertEquals(RinPakTrust.Status.TRUSTED, a.status)
        assertEquals("Droy", a.label)
        t.untrust(k2)
        assertFalse(t.isTrusted(k2))
        assertEquals(RinPakTrust.Status.KEY_CHANGED, t.assess("App", k2, own).status)
    }

    @Test fun unsignedImportsAreNotRecorded() {
        val t = RinPakTrust(MemStore())
        t.recordImport("Other", null)
        assertEquals(RinPakTrust.Status.NEW_SIGNER, t.assess("Other", k1, own).status)
    }
}
