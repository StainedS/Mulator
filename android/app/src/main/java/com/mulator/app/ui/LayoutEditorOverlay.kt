package com.mulator.app.ui

import androidx.compose.animation.AnimatedContent
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.SwapVert
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Slider
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.mulator.app.ControllerView
import com.mulator.app.R
import com.mulator.app.data.Control
import kotlin.math.roundToInt

class LayoutEditorState {
    var selected by mutableStateOf<Control?>(null)
    var scale by mutableFloatStateOf(1f)
    var dirty by mutableStateOf(false)
    var toolbarAtTop by mutableStateOf(true)
    var confirmDiscard by mutableStateOf(false)
}

interface LayoutEditorActions {
    fun setScale(scale: Float)
    fun resetSelected()
    fun resetAll()
    fun save()
    fun cancel()
    fun discard()
}

/**
 * Floating toolbar for the layout editor. It only covers its own area, so the
 * controls underneath stay draggable; it can be moved to the other edge if it
 * is in the way.
 */
@Composable
fun LayoutEditorOverlay(state: LayoutEditorState, actions: LayoutEditorActions) {
    val config = LocalConfiguration.current
    val portrait = config.screenHeightDp > config.screenWidthDp
    Box(Modifier.fillMaxSize()) {
        Surface(
            shape = MaterialTheme.shapes.large,
            color = MaterialTheme.colorScheme.surfaceContainerLow.copy(alpha = 0.95f),
            tonalElevation = 3.dp,
            shadowElevation = 8.dp,
            modifier = Modifier
                .align(if (state.toolbarAtTop) Alignment.TopCenter else Alignment.BottomCenter)
                .padding(12.dp)
                .widthIn(max = 560.dp)
                .fillMaxWidth(),
        ) {
            Column(Modifier.padding(horizontal = 16.dp, vertical = 12.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Column(Modifier.weight(1f)) {
                        Text(stringResource(R.string.edit_layout), style = MaterialTheme.typography.titleMedium)
                        Text(
                            stringResource(if (portrait) R.string.editing_portrait else R.string.editing_landscape),
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                    IconButton(onClick = { state.toolbarAtTop = !state.toolbarAtTop }) {
                        Icon(Icons.Rounded.SwapVert, stringResource(R.string.move_toolbar))
                    }
                }
                Spacer(Modifier.height(8.dp))
                AnimatedContent(state.selected, label = "selection") { selected ->
                    if (selected == null) {
                        Text(
                            stringResource(R.string.edit_layout_hint),
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            modifier = Modifier.padding(vertical = 8.dp),
                        )
                    } else {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Column(Modifier.width(76.dp)) {
                                Text(stringResource(selected.title), style = MaterialTheme.typography.labelLarge, maxLines = 1)
                                Text(
                                    "${(state.scale * 100).roundToInt()}%",
                                    style = MaterialTheme.typography.labelSmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                                )
                            }
                            Slider(
                                value = state.scale,
                                onValueChange = actions::setScale,
                                valueRange = ControllerView.MIN_SCALE..ControllerView.MAX_SCALE,
                                modifier = Modifier.weight(1f),
                            )
                            TextButton(onClick = actions::resetSelected) { Text(stringResource(R.string.reset_button)) }
                        }
                    }
                }
                Spacer(Modifier.height(4.dp))
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
                    OutlinedButton(onClick = actions::resetAll) { Text(stringResource(R.string.reset_all)) }
                    Spacer(Modifier.weight(1f))
                    TextButton(onClick = actions::cancel) { Text(stringResource(R.string.cancel)) }
                    Button(onClick = actions::save) { Text(stringResource(R.string.save)) }
                }
            }
        }
    }

    if (state.confirmDiscard) {
        AlertDialog(
            onDismissRequest = { state.confirmDiscard = false },
            title = { Text(stringResource(R.string.discard_title)) },
            text = { Text(stringResource(R.string.discard_body)) },
            confirmButton = { TextButton(onClick = actions::discard) { Text(stringResource(R.string.discard)) } },
            dismissButton = { TextButton(onClick = { state.confirmDiscard = false }) { Text(stringResource(R.string.keep_editing)) } },
        )
    }
}
