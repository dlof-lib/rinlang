package com.dlof.rinlang

/**
 * Kotlin gateway into the native Rin language engine written in C++.
 * The C++ implementation lives under app/src/main/cpp and is compiled
 * into libRinengine.so via CMake / the Android NDK.
 */
object RinEngine {

    init {
        System.loadLibrary("rinengine")
    }

    /** Lexes, parses and interprets [source]; returns everything the program printed. */
    external fun runSource(source: String): String

    /** Returns a human readable version string for the native engine. */
    external fun engineVersion(): String
}
