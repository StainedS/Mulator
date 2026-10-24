package com.mulator.app.ui

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleIn
import androidx.compose.animation.scaleOut
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.ExitToApp
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.FastForward
import androidx.compose.material.icons.rounded.PlayArrow
import androidx.compose.material.icons.rounded.RestartAlt
import androidx.compose.material.icons.rounded.Tune
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.mulator.app.R
import com.mulator.app.data.SaveSlots
import kotlinx.coroutines.delay

/** A short message shown at the top of the game screen, optionally with one action. */
data class Notice(
    val text: String,
    val actionLabel: String? = null,
    val action: (() -> Unit)? = null,
    val id: Long = System.nanoTime(),
)

/** Everything drawn on top of the running game; state lives in EmulatorActivity. */
class GameOverlayState {
    var menuOpen by mutableStateOf(false)
    var fastForward by mutableStateOf(false)
    var notice by mutableStateOf<Notice?>(null)
    /** Bumped after a slot is written so thumbnails reload. */
    var slotsVersion by mutableStateOf(0)
    /** Focus the first button when the menu opens (for gamepad users). */
    var focusOnOpen by mutableStateOf(false)
}

interface GameMenuActions {
    fun resume()
    fun saveSlot(slot: Int)
    fun loadSlot(slot: Int)
    fun toggleFastForward()
    fun reset()
    fun quit()
    fun editControls()
}

@Composable
fun GameOverlay(state: GameOverlayState, title: String, slots: SaveSlots, ffSpeed: Int, actions: GameMenuActions) {
    Box(Modifier.fillMaxSize()) {
        AnimatedVisibility(state.menuOpen, enter = fadeIn(), exit = fadeOut()) {
            Box(
                Modifier
                    .fillMaxSize()
                    .background(Color.Black.copy(alpha = 0.72f))
                    .clickable(interactionSource = remember { MutableInteractionSource() }, indication = null) { actions.resume() },
                contentAlignment = Alignment.Center,
            ) {
                AnimatedVisibility(state.menuOpen, enter = scaleIn(initialScale = 0.92f) + fadeIn(), exit = scaleOut(targetScale = 0.95f) + fadeOut()) {
                    PauseMenu(state, title, slots, ffSpeed, actions)
                }
            }
        }
        NoticePill(state, Modifier.align(Alignment.TopCenter))
    }
}

@Composable
private fun PauseMenu(state: GameOverlayState, title: String, slots: SaveSlots, ffSpeed: Int, actions: GameMenuActions) {
    val resumeFocus = remember { FocusRequester() }
    var confirmReset by remember { mutableStateOf(false) }
    LaunchedEffect(Unit) {
        if (state.focusOnOpen) try { resumeFocus.requestFocus() } catch (e: IllegalStateException) {}
    }
    Surface(
        shape = MaterialTheme.shapes.extraLarge,
        color = MaterialTheme.colorScheme.surfaceContainerLow,
        tonalElevation = 2.dp,
        modifier = Modifier
            .padding(16.dp)
            .widthIn(max = 600.dp)
            .fillMaxWidth()
            // Swallow taps so they don't reach the scrim behind the panel.
            .clickable(interactionSource = remember { MutableInteractionSource() }, indication = null) {},
    ) {
        Column(Modifier.verticalScroll(rememberScrollState()).padding(20.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Text(
                        stringResource(R.string.paused).uppercase(),
                        style = MaterialTheme.typography.labelMedium,
                        color = MaterialTheme.colorScheme.primary,
                    )
                    Text(title, style = MaterialTheme.typography.titleLarge, maxLines = 1, overflow = TextOverflow.Ellipsis)
                }
                IconButton(onClick = actions::editControls) { Icon(Icons.Rounded.Tune, stringResource(R.string.edit_controls)) }
                IconButton(onClick = actions::resume) { Icon(Icons.Rounded.Close, stringResource(R.string.menu_resume)) }
            }
            Spacer(Modifier.height(16.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                ActionTile(
                    Icons.Rounded.PlayArrow, stringResource(R.string.menu_resume), highlighted = true,
                    onClick = actions::resume, modifier = Modifier.weight(1f).focusRequester(resumeFocus),
                )
                ActionTile(
                    Icons.Rounded.FastForward,
                    if (ffSpeed == 0) "∞" else "${ffSpeed}×",
                    highlighted = state.fastForward,
                    onClick = actions::toggleFastForward,
                    modifier = Modifier.weight(1f),
                )
                ActionTile(
                    Icons.Rounded.RestartAlt,
                    stringResource(if (confirmReset) R.string.menu_reset_confirm else R.string.menu_reset),
                    highlighted = false,
                    danger = confirmReset,
                    onClick = { if (confirmReset) actions.reset() else confirmReset = true },
                    modifier = Modifier.weight(1f),
                )
                ActionTile(
                    Icons.AutoMirrored.Rounded.ExitToApp, stringResource(R.string.menu_quit), highlighted = false,
                    onClick = actions::quit, modifier = Modifier.weight(1f),
                )
            }
            Spacer(Modifier.height(22.dp))
            Text(stringResource(R.string.save_states), style = MaterialTheme.typography.titleMedium)
            Spacer(Modifier.height(10.dp))
            val version = state.slotsVersion
            LazyRow(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                val shown = buildList {
                    if (slots.exists(SaveSlots.AUTO_SLOT)) add(SaveSlots.AUTO_SLOT)
                    addAll(1..SaveSlots.MANUAL_SLOTS)
                }
                items(shown, key = { it }) { slot ->
                    SlotCard(slots, slot, version, onSave = { actions.saveSlot(slot) }, onLoad = { actions.loadSlot(slot) })
                }
            }
        }
    }
}

@Composable
private fun ActionTile(
    icon: ImageVector,
    label: String,
    highlighted: Boolean,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    danger: Boolean = false,
) {
    val colors = MaterialTheme.colorScheme
    val bg = when {
        danger -> colors.errorContainer
        highlighted -> colors.primary
        else -> colors.surfaceContainerHighest
    }
    val fg = when {
        danger -> colors.onErrorContainer
        highlighted -> colors.onPrimary
        else -> colors.onSurface
    }
    Surface(onClick = onClick, shape = MaterialTheme.shapes.medium, color = bg, contentColor = fg, modifier = modifier.height(76.dp)) {
        Column(horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.Center) {
            Icon(icon, null, Modifier.size(26.dp))
            Spacer(Modifier.height(4.dp))
            Text(
                label,
                style = MaterialTheme.typography.labelMedium,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                textAlign = TextAlign.Center,
                modifier = Modifier.padding(horizontal = 4.dp),
            )
        }
    }
}

@Composable
private fun SlotCard(slots: SaveSlots, slot: Int, version: Int, onSave: () -> Unit, onLoad: () -> Unit) {
    val context = LocalContext.current
    val exists = slots.exists(slot)
    // version is read so this recomposes (and re-checks the file) after a save.
    val shot = rememberThumbnail(slots.thumbnail(slot).takeIf { version >= 0 && it.exists() })
    Column(Modifier.width(150.dp)) {
        Box(
            Modifier
                .fillMaxWidth()
                .aspectRatio(3f / 2f)
                .clip(MaterialTheme.shapes.small)
                .background(MaterialTheme.colorScheme.surfaceContainerHighest),
            contentAlignment = Alignment.Center,
        ) {
            if (shot != null) {
                Image(shot, null, contentScale = ContentScale.Crop, filterQuality = FilterQuality.None, modifier = Modifier.fillMaxSize())
            } else {
                Text(stringResource(R.string.slot_empty), style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
        }
        Text(
            if (slot == SaveSlots.AUTO_SLOT) stringResource(R.string.slot_auto) else stringResource(R.string.slot_n, slot),
            style = MaterialTheme.typography.labelLarge,
            modifier = Modifier.padding(top = 6.dp),
        )
        Text(
            if (exists) relativeTime(context, slots.savedAt(slot)) else "—",
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            maxLines = 1,
        )
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp), modifier = Modifier.padding(top = 6.dp)) {
            if (slot != SaveSlots.AUTO_SLOT) {
                FilledTonalButton(onClick = onSave, modifier = Modifier.weight(1f).height(36.dp), contentPadding = SmallButtonPadding) {
                    Text(stringResource(R.string.save))
                }
            }
            FilledTonalButton(
                onClick = onLoad,
                enabled = exists,
                modifier = Modifier.weight(1f).height(36.dp),
                contentPadding = SmallButtonPadding,
            ) { Text(stringResource(R.string.load)) }
        }
    }
}

private val SmallButtonPadding = PaddingValues(horizontal = 8.dp)

@Composable
private fun NoticePill(state: GameOverlayState, modifier: Modifier) {
    val notice = state.notice
    LaunchedEffect(notice?.id) {
        if (notice != null) {
            delay(if (notice.action != null) 5000 else 2200)
            if (state.notice?.id == notice.id) state.notice = null
        }
    }
    AnimatedVisibility(
        notice != null,
        enter = slideInVertically { -it } + fadeIn(),
        exit = slideOutVertically { -it } + fadeOut(),
        modifier = modifier.padding(top = 20.dp, start = 16.dp, end = 16.dp),
    ) {
        // Keep showing the last notice while it animates out.
        val shown = remember { mutableStateOf(notice) }
        if (notice != null) shown.value = notice
        val n = shown.value ?: return@AnimatedVisibility
        Surface(shape = CircleShape, color = MaterialTheme.colorScheme.inverseSurface, contentColor = MaterialTheme.colorScheme.inverseOnSurface) {
            Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.padding(start = 20.dp, end = if (n.action != null) 6.dp else 20.dp)) {
                Text(n.text, style = MaterialTheme.typography.bodyMedium, modifier = Modifier.padding(vertical = 12.dp))
                if (n.action != null && n.actionLabel != null) {
                    TextButton(onClick = { state.notice = null; n.action.invoke() }) {
                        Text(n.actionLabel, color = MaterialTheme.colorScheme.inversePrimary)
                    }
                }
            }
        }
    }
}
