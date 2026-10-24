package com.mulator.app

import android.view.Surface

/** Kotlin face of the native emulator (see app/src/main/cpp/jni_bridge.cpp). */
object NativeBridge {
    init {
        System.loadLibrary("mulator")
    }

    // Key bits, matching mulator::Key in core/include/mulator/machine.h.
    const val KEY_A = 1 shl 0
    const val KEY_B = 1 shl 1
    const val KEY_SELECT = 1 shl 2
    const val KEY_START = 1 shl 3
    const val KEY_RIGHT = 1 shl 4
    const val KEY_LEFT = 1 shl 5
    const val KEY_UP = 1 shl 6
    const val KEY_DOWN = 1 shl 7
    const val KEY_R = 1 shl 8
    const val KEY_L = 1 shl 9

    const val SCREEN_WIDTH = 240
    const val SCREEN_HEIGHT = 160

    @JvmStatic external fun loadRom(rom: ByteArray, savePath: String): Boolean
    @JvmStatic external fun romTitle(): String
    @JvmStatic external fun start()
    @JvmStatic external fun stop()
    @JvmStatic external fun setPaused(paused: Boolean)
    @JvmStatic external fun setSurface(surface: Surface?, width: Int, height: Int)
    @JvmStatic external fun setKeys(keys: Int)
    @JvmStatic external fun setFastForward(on: Boolean)
    /** Multiplier while fast-forwarding; 0 = unlimited. */
    @JvmStatic external fun setFastForwardSpeed(speed: Int)
    @JvmStatic external fun setRewindEnabled(on: Boolean)
    /** While true the game runs backwards through its recent history. */
    @JvmStatic external fun setRewinding(on: Boolean)
    /** Current frame as [SCREEN_WIDTH] x [SCREEN_HEIGHT] ARGB pixels. */
    @JvmStatic external fun screenshot(): IntArray
    @JvmStatic external fun reset()
    @JvmStatic external fun saveState(path: String): Boolean
    @JvmStatic external fun loadState(path: String): Boolean
    @JvmStatic external fun flushSave()
}
