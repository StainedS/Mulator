package com.mulator.app

import android.content.Context
import android.content.Intent
import android.content.pm.ShortcutInfo
import android.content.pm.ShortcutManager
import android.graphics.Bitmap
import android.graphics.drawable.Icon
import com.mulator.app.data.Game
import com.mulator.app.data.Library
import com.mulator.app.data.SaveSlots

/** Launcher shortcuts: long-press the app icon for recent games, or pin a game to the home screen. */
object Shortcuts {
    private const val MAX_RECENT = 3

    fun playIntent(context: Context, game: Game): Intent =
        Intent(context, MainActivity::class.java)
            .setAction(MainActivity.ACTION_PLAY)
            .setData(game.uri)

    fun pin(context: Context, game: Game): Boolean {
        val manager = context.getSystemService(ShortcutManager::class.java) ?: return false
        if (!manager.isRequestPinShortcutSupported) return false
        return manager.requestPinShortcut(build(context, game), null)
    }

    /** Refreshes the long-press menu with the most recently played games. */
    fun updateRecent(context: Context) {
        val manager = context.getSystemService(ShortcutManager::class.java) ?: return
        val recent = Library.visibleGames.filter { it.lastPlayed > 0 }.sortedByDescending { it.lastPlayed }.take(MAX_RECENT)
        try {
            manager.dynamicShortcuts = recent.map { build(context, it) }
        } catch (e: IllegalStateException) {
            // Rate limited while in the background; the next update will catch up.
        }
    }

    private fun build(context: Context, game: Game): ShortcutInfo =
        ShortcutInfo.Builder(context, game.id)
            .setShortLabel(game.title.take(24))
            .setLongLabel(game.title)
            .setIcon(icon(context, game))
            .setIntent(playIntent(context, game))
            .build()

    private fun icon(context: Context, game: Game): Icon {
        val shot = SaveSlots(context, game.id).latestThumbnail()?.let(SaveSlots::decode)
            ?: return Icon.createWithResource(context, R.mipmap.ic_launcher)
        // Centre square, scaled up with nearest-neighbour so pixels stay crisp.
        val side = minOf(shot.width, shot.height)
        val square = Bitmap.createBitmap(shot, (shot.width - side) / 2, (shot.height - side) / 2, side, side)
        val scaled = Bitmap.createScaledBitmap(square, side * 2, side * 2, false)
        return Icon.createWithBitmap(scaled)
    }
}
