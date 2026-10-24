package com.mulator.app.data

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import java.io.File

/**
 * Save-state files for one game: slot 0 is the automatic "resume" state written
 * when leaving a game, slots 1..[MANUAL_SLOTS] are the player's own. Each state
 * has a PNG screenshot next to it.
 *
 *   files/saves/<id>.sav             battery save (written by the native side)
 *   files/states/<id>/slot<n>.state  save state
 *   files/states/<id>/slot<n>.png    its thumbnail
 */
class SaveSlots(context: Context, private val gameId: String) {
    private val filesDir = context.filesDir
    private val dir = File(filesDir, "states/$gameId")

    init {
        // Version 0.1 had a single state per game at states/<id>.state.
        val legacy = File(filesDir, "states/$gameId.state")
        if (legacy.exists()) {
            dir.mkdirs()
            if (!state(1).exists()) legacy.renameTo(state(1)) else legacy.delete()
        }
    }

    val batterySave: File get() = File(filesDir, "saves").apply { mkdirs() }.let { File(it, "$gameId.sav") }

    fun state(slot: Int) = File(dir.apply { mkdirs() }, "slot$slot.state")
    fun thumbnail(slot: Int) = File(dir, "slot$slot.png")
    fun exists(slot: Int) = state(slot).exists()
    fun savedAt(slot: Int): Long = state(slot).lastModified()

    fun writeThumbnail(slot: Int, argb: IntArray, width: Int, height: Int) = writePng(thumbnail(slot), argb, width, height)

    fun delete(slot: Int) {
        state(slot).delete()
        thumbnail(slot).delete()
    }

    /** Thumbnail of the most recent state, used as the game's cover in the library. */
    fun latestThumbnail(): File? =
        (0..MANUAL_SLOTS).map(::thumbnail).filter { it.exists() }.maxByOrNull { it.lastModified() }

    companion object {
        const val AUTO_SLOT = 0
        const val MANUAL_SLOTS = 4

        fun writePng(file: File, argb: IntArray, width: Int, height: Int) {
            val bmp = Bitmap.createBitmap(argb, width, height, Bitmap.Config.ARGB_8888)
            try {
                file.outputStream().use { bmp.compress(Bitmap.CompressFormat.PNG, 100, it) }
            } finally {
                bmp.recycle()
            }
        }

        fun decode(file: File): Bitmap? = try {
            BitmapFactory.decodeFile(file.path)
        } catch (e: Exception) {
            null
        }
    }
}
