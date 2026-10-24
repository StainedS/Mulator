package com.mulator.app.ui

import android.content.Context
import android.net.Uri
import android.os.Build
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.ArrowBack
import androidx.compose.material.icons.automirrored.rounded.KeyboardArrowRight
import androidx.compose.material.icons.rounded.CheckCircle
import androidx.compose.material.icons.rounded.OpenWith
import androidx.compose.material.icons.rounded.RestartAlt
import androidx.compose.material.icons.rounded.CreateNewFolder
import androidx.compose.material.icons.rounded.Folder
import androidx.compose.material.icons.rounded.RemoveCircleOutline
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Slider
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import com.mulator.app.ControllerView
import com.mulator.app.R
import com.mulator.app.data.ControllerLayouts
import com.mulator.app.data.ControllerSkins
import com.mulator.app.data.Library
import com.mulator.app.data.Settings
import kotlin.math.roundToInt

private val FAST_FORWARD_SPEEDS = listOf(2, 3, 4, 8, 0)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(onBack: () -> Unit, onAddFolder: () -> Unit, onEditLayout: () -> Unit) {
    val context = LocalContext.current
    Scaffold(
        containerColor = MaterialTheme.colorScheme.background,
        topBar = {
            TopAppBar(
                title = { Text(stringResource(R.string.settings)) },
                navigationIcon = {
                    IconButton(onClick = onBack) { Icon(Icons.AutoMirrored.Rounded.ArrowBack, stringResource(R.string.back)) }
                },
                colors = TopAppBarDefaults.topAppBarColors(containerColor = MaterialTheme.colorScheme.background),
            )
        },
    ) { padding ->
        Column(
            Modifier
                .fillMaxSize()
                .padding(padding)
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 16.dp)
                .padding(bottom = 32.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            val width = Modifier.widthIn(max = 640.dp).fillMaxWidth()

            Section(R.string.section_gameplay, width) {
                SwitchRow(R.string.setting_resume, R.string.setting_resume_hint, Settings.resumeOnLaunch) { Settings.resumeOnLaunch = it }
                SwitchRow(R.string.setting_rewind, R.string.setting_rewind_hint, Settings.rewindEnabled) { Settings.rewindEnabled = it }
                Column(Modifier.padding(horizontal = 16.dp, vertical = 12.dp)) {
                    Text(stringResource(R.string.setting_ff_speed), style = MaterialTheme.typography.bodyLarge)
                    Text(
                        stringResource(R.string.setting_ff_speed_hint),
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                    SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth().padding(top = 10.dp)) {
                        FAST_FORWARD_SPEEDS.forEachIndexed { i, speed ->
                            SegmentedButton(
                                selected = Settings.fastForwardSpeed == speed,
                                onClick = { Settings.fastForwardSpeed = speed },
                                shape = SegmentedButtonDefaults.itemShape(i, FAST_FORWARD_SPEEDS.size),
                                label = { Text(if (speed == 0) "∞" else "${speed}×") },
                            )
                        }
                    }
                }
            }

            Section(R.string.section_controls, width) {
                Text(
                    stringResource(R.string.setting_button_style),
                    style = MaterialTheme.typography.bodyLarge,
                    modifier = Modifier.padding(start = 16.dp, end = 16.dp, top = 16.dp, bottom = 10.dp),
                )
                SkinPicker()
                ListItem(
                    leadingContent = { Icon(Icons.Rounded.OpenWith, null, tint = MaterialTheme.colorScheme.primary) },
                    headlineContent = { Text(stringResource(R.string.setting_custom_layout)) },
                    supportingContent = { Text(stringResource(R.string.setting_custom_layout_hint)) },
                    trailingContent = { Icon(Icons.AutoMirrored.Rounded.KeyboardArrowRight, null) },
                    colors = ListItemDefaults.colors(containerColor = Color.Transparent),
                    modifier = Modifier.clickable(onClick = onEditLayout),
                )
                // Re-read when a layout is saved or reset.
                val hasCustom = remember(ControllerLayouts.version) { ControllerLayouts.hasCustom() }
                if (hasCustom) {
                    ListItem(
                        leadingContent = { Icon(Icons.Rounded.RestartAlt, null) },
                        headlineContent = { Text(stringResource(R.string.setting_reset_layout)) },
                        supportingContent = { Text(stringResource(R.string.setting_reset_layout_hint)) },
                        colors = ListItemDefaults.colors(containerColor = Color.Transparent),
                        modifier = Modifier.clickable {
                            ControllerLayouts.save(portrait = true, layout = null)
                            ControllerLayouts.save(portrait = false, layout = null)
                        },
                    )
                }
                SwitchRow(R.string.setting_haptics, R.string.setting_haptics_hint, Settings.haptics) { Settings.haptics = it }
                SliderRow(
                    R.string.setting_opacity,
                    "${(Settings.controllerOpacity * 100).roundToInt()}%",
                    Settings.controllerOpacity,
                    0.2f..1f,
                ) { Settings.controllerOpacity = it }
                SliderRow(
                    R.string.setting_size,
                    "${(Settings.controllerScale * 100).roundToInt()}%",
                    Settings.controllerScale,
                    0.75f..1.3f,
                ) { Settings.controllerScale = it }
            }

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                Section(R.string.section_appearance, width) {
                    SwitchRow(R.string.setting_dynamic_color, R.string.setting_dynamic_color_hint, Settings.dynamicColor) {
                        Settings.dynamicColor = it
                    }
                }
            }

            Section(R.string.section_folders, width) {
                if (Library.folders.isEmpty()) {
                    Text(
                        stringResource(R.string.no_folders),
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        modifier = Modifier.padding(16.dp),
                    )
                }
                Library.folders.forEach { folder ->
                    ListItem(
                        leadingContent = { Icon(Icons.Rounded.Folder, null) },
                        headlineContent = { Text(folderLabel(folder), maxLines = 1, overflow = TextOverflow.Ellipsis) },
                        trailingContent = {
                            IconButton(onClick = { Library.removeFolder(context, folder) }) {
                                Icon(Icons.Rounded.RemoveCircleOutline, stringResource(R.string.remove))
                            }
                        },
                        colors = ListItemDefaults.colors(containerColor = Color.Transparent),
                    )
                }
                ListItem(
                    leadingContent = { Icon(Icons.Rounded.CreateNewFolder, null, tint = MaterialTheme.colorScheme.primary) },
                    headlineContent = { Text(stringResource(R.string.add_folder), color = MaterialTheme.colorScheme.primary) },
                    colors = ListItemDefaults.colors(containerColor = Color.Transparent),
                    modifier = Modifier.clickable(onClick = onAddFolder),
                )
            }

            Section(R.string.section_about, width) {
                ListItem(
                    headlineContent = { Text(stringResource(R.string.app_name)) },
                    supportingContent = { Text(stringResource(R.string.version_n, versionName(context))) },
                    colors = ListItemDefaults.colors(containerColor = Color.Transparent),
                )
                Text(
                    stringResource(R.string.legal_note),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(start = 16.dp, end = 16.dp, bottom = 16.dp),
                )
            }
        }
    }
}

private fun versionName(context: Context): String =
    context.packageManager.getPackageInfo(context.packageName, 0).versionName ?: ""

/** One card per controller style, each showing a live miniature of the touch controls. */
@Composable
private fun SkinPicker() {
    LazyRow(
        horizontalArrangement = Arrangement.spacedBy(10.dp),
        contentPadding = PaddingValues(horizontal = 16.dp),
        modifier = Modifier.padding(bottom = 8.dp),
    ) {
        items(ControllerSkins.all, key = { it.id }) { skin ->
            val selected = Settings.controllerSkin == skin.id
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                Box(
                    Modifier
                        .size(width = 112.dp, height = 132.dp)
                        .clip(MaterialTheme.shapes.medium)
                        .background(Brush.verticalGradient(listOf(Color(0xFF1E1A2E), Color(0xFF0B0B10))))
                        .border(
                            width = if (selected) 2.5.dp else 1.dp,
                            color = if (selected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.outlineVariant,
                            shape = MaterialTheme.shapes.medium,
                        )
                        .clickable { Settings.controllerSkin = skin.id },
                ) {
                    AndroidView(
                        factory = { ControllerView(it).apply { interactive = false } },
                        update = { it.skin = skin },
                        modifier = Modifier.fillMaxSize().padding(4.dp),
                    )
                    if (selected) {
                        Icon(
                            Icons.Rounded.CheckCircle,
                            null,
                            tint = MaterialTheme.colorScheme.primary,
                            modifier = Modifier.align(Alignment.TopEnd).padding(6.dp).size(20.dp),
                        )
                    }
                }
                Text(
                    stringResource(skin.title),
                    style = MaterialTheme.typography.labelLarge,
                    color = if (selected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurface,
                    modifier = Modifier.padding(top = 6.dp),
                )
            }
        }
    }
}

/** "content://…/tree/primary%3AGames%2FHandheld" -> "Games/Handheld" */
private fun folderLabel(tree: Uri): String =
    Uri.decode(tree.lastPathSegment ?: tree.toString()).substringAfter(':').ifBlank { "/" }

@Composable
private fun Section(title: Int, modifier: Modifier, content: @Composable () -> Unit) {
    Column(modifier.padding(top = 20.dp)) {
        Text(
            stringResource(title).uppercase(),
            style = MaterialTheme.typography.labelMedium,
            color = MaterialTheme.colorScheme.primary,
            modifier = Modifier.padding(start = 16.dp, bottom = 8.dp),
        )
        Surface(shape = MaterialTheme.shapes.medium, color = MaterialTheme.colorScheme.surfaceContainer) {
            Column { content() }
        }
    }
}

@Composable
private fun SwitchRow(title: Int, subtitle: Int, checked: Boolean, onChange: (Boolean) -> Unit) {
    ListItem(
        headlineContent = { Text(stringResource(title)) },
        supportingContent = { Text(stringResource(subtitle)) },
        trailingContent = { Switch(checked = checked, onCheckedChange = onChange) },
        colors = ListItemDefaults.colors(containerColor = Color.Transparent),
        modifier = Modifier.clickable { onChange(!checked) },
    )
}

@Composable
private fun SliderRow(title: Int, valueLabel: String, value: Float, range: ClosedFloatingPointRange<Float>, onChange: (Float) -> Unit) {
    Column(Modifier.padding(horizontal = 16.dp, vertical = 8.dp)) {
        Row {
            Text(stringResource(title), style = MaterialTheme.typography.bodyLarge, modifier = Modifier.weight(1f))
            Text(valueLabel, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        Slider(value = value, onValueChange = onChange, valueRange = range)
    }
}
