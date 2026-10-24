package com.mulator.app.ui

import android.content.Context
import android.text.format.DateUtils
import android.text.format.Formatter
import android.util.LruCache
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.sp
import androidx.compose.material3.Text
import com.mulator.app.R
import com.mulator.app.data.Game
import com.mulator.app.data.SaveSlots
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import kotlin.math.absoluteValue

private val thumbnailCache = LruCache<String, ImageBitmap>(48)

/** Loads a save-state screenshot off the main thread; null while loading or if missing. */
@Composable
fun rememberThumbnail(file: File?): ImageBitmap? {
    val key = file?.let { "${it.path}@${it.lastModified()}" }
    val image by produceState(key?.let { thumbnailCache.get(it) }, key) {
        if (file == null || key == null || value != null) return@produceState
        value = withContext(Dispatchers.IO) {
            SaveSlots.decode(file)?.asImageBitmap()?.also { thumbnailCache.put(key, it) }
        }
    }
    return image
}

/**
 * Cover art for a game: the screenshot from its latest save state if there is
 * one, otherwise a generated gradient with the title's initials.
 */
@Composable
fun GameArt(game: Game, modifier: Modifier = Modifier) {
    val context = LocalContext.current
    // lastPlayed changes whenever a session ends, which is when new screenshots appear.
    val file = remember(game.id, game.lastPlayed) { SaveSlots(context, game.id).latestThumbnail() }
    val shot = rememberThumbnail(file)
    if (shot != null) {
        Image(
            bitmap = shot,
            contentDescription = null,
            contentScale = ContentScale.Crop,
            filterQuality = FilterQuality.None, // keep pixels sharp
            modifier = modifier,
        )
    } else {
        GeneratedArt(game.title, modifier)
    }
}

@Composable
fun GeneratedArt(title: String, modifier: Modifier = Modifier) {
    val hue = (title.hashCode().absoluteValue % 360).toFloat()
    val brush = Brush.linearGradient(
        listOf(Color.hsv(hue, 0.55f, 0.55f), Color.hsv((hue + 40f) % 360f, 0.65f, 0.22f)),
    )
    val initials = title.split(' ', '-', ':').filter { it.firstOrNull()?.isLetterOrDigit() == true }
        .take(2).joinToString("") { it.first().uppercase() }.ifEmpty { "?" }
    BoxWithConstraints(modifier.background(brush), contentAlignment = Alignment.Center) {
        Text(
            initials,
            color = Color.White.copy(alpha = 0.92f),
            fontWeight = FontWeight.Black,
            fontSize = (maxHeight.value * 0.36f).sp,
            textAlign = TextAlign.Center,
        )
        Box(
            Modifier.fillMaxSize().background(
                Brush.verticalGradient(listOf(Color.Transparent, Color.Black.copy(alpha = 0.25f)))
            )
        )
    }
}

// ---------------------------------------------------------------- formatting
fun relativeTime(context: Context, millis: Long): String =
    if (millis <= 0) context.getString(R.string.never_played)
    else if (System.currentTimeMillis() - millis < DateUtils.MINUTE_IN_MILLIS) context.getString(R.string.just_now)
    else DateUtils.getRelativeTimeSpanString(millis, System.currentTimeMillis(), DateUtils.MINUTE_IN_MILLIS).toString()

fun playTime(context: Context, millis: Long): String {
    val minutes = millis / 60_000
    return when {
        minutes < 1 -> context.getString(R.string.playtime_none)
        minutes < 60 -> context.getString(R.string.playtime_minutes, minutes)
        else -> context.getString(R.string.playtime_hours, minutes / 60, minutes % 60)
    }
}

fun fileSize(context: Context, bytes: Long): String = Formatter.formatShortFileSize(context, bytes)
