package com.mulator.app.ui

import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.AddToHomeScreen
import androidx.compose.material.icons.automirrored.rounded.InsertDriveFile
import androidx.compose.material.icons.rounded.Add
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.CreateNewFolder
import androidx.compose.material.icons.rounded.Delete
import androidx.compose.material.icons.rounded.MoreVert
import androidx.compose.material.icons.rounded.PlayArrow
import androidx.compose.material.icons.rounded.Replay
import androidx.compose.material.icons.rounded.Search
import androidx.compose.material.icons.rounded.Settings
import androidx.compose.material.icons.rounded.SportsEsports
import androidx.compose.material.icons.rounded.Star
import androidx.compose.material.icons.rounded.StarBorder
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExtendedFloatingActionButton
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextField
import androidx.compose.material3.TextFieldDefaults
import androidx.compose.material3.pulltorefresh.PullToRefreshBox
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
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
import com.mulator.app.data.Game
import com.mulator.app.data.Library
import com.mulator.app.data.SaveSlots
import kotlinx.coroutines.launch

enum class LibraryFilter { ALL, FAVORITES, RECENT }

/** What to do when a game starts. */
data class LaunchOptions(val fresh: Boolean = false, val slot: Int? = null)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun LibraryScreen(
    onPlay: (Game, LaunchOptions) -> Unit,
    onAddFolder: () -> Unit,
    onAddFile: () -> Unit,
    onOpenSettings: () -> Unit,
    onPinShortcut: (Game) -> Unit,
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var query by rememberSaveable { mutableStateOf("") }
    var searching by rememberSaveable { mutableStateOf(false) }
    var filter by rememberSaveable { mutableStateOf(LibraryFilter.ALL) }
    var refreshing by remember { mutableStateOf(false) }
    var showAddSheet by remember { mutableStateOf(false) }
    var detailsFor by remember { mutableStateOf<Game?>(null) }

    val all = Library.visibleGames
    val lastPlayed = all.filter { it.lastPlayed > 0 }.maxByOrNull { it.lastPlayed }
    val shown = all
        .filter { query.isBlank() || it.title.contains(query, ignoreCase = true) }
        .filter {
            when (filter) {
                LibraryFilter.ALL -> true
                LibraryFilter.FAVORITES -> it.favorite
                LibraryFilter.RECENT -> it.lastPlayed > 0
            }
        }
        .let { list ->
            if (filter == LibraryFilter.RECENT) list.sortedByDescending { it.lastPlayed }
            else list.sortedBy { it.title.lowercase() }
        }

    Scaffold(
        containerColor = MaterialTheme.colorScheme.background,
        floatingActionButton = {
            if (all.isNotEmpty()) {
                ExtendedFloatingActionButton(
                    onClick = { showAddSheet = true },
                    icon = { Icon(Icons.Rounded.Add, null) },
                    text = { Text(stringResource(R.string.add_games)) },
                    containerColor = MaterialTheme.colorScheme.primaryContainer,
                    contentColor = MaterialTheme.colorScheme.onPrimaryContainer,
                )
            }
        },
    ) { padding ->
        PullToRefreshBox(
            isRefreshing = refreshing,
            onRefresh = {
                scope.launch {
                    refreshing = true
                    Library.rescan(context)
                    refreshing = false
                }
            },
            modifier = Modifier.fillMaxSize().padding(padding),
        ) {
            if (all.isEmpty()) {
                EmptyLibrary(onAddFolder, onAddFile, onOpenSettings)
                return@PullToRefreshBox
            }
            LazyVerticalGrid(
                columns = GridCells.Adaptive(152.dp),
                contentPadding = PaddingValues(start = 16.dp, end = 16.dp, top = 8.dp, bottom = 104.dp),
                horizontalArrangement = Arrangement.spacedBy(12.dp),
                verticalArrangement = Arrangement.spacedBy(16.dp),
                modifier = Modifier.fillMaxSize(),
            ) {
                item(span = { GridItemSpan(maxLineSpan) }) {
                    Header(
                        count = all.size,
                        searching = searching,
                        onSearch = { searching = !searching; if (!searching) query = "" },
                        onOpenSettings = onOpenSettings,
                    )
                }
                if (searching) {
                    item(span = { GridItemSpan(maxLineSpan) }) {
                        SearchField(query, onChange = { query = it })
                    }
                }
                if (lastPlayed != null && query.isBlank() && filter == LibraryFilter.ALL) {
                    item(span = { GridItemSpan(maxLineSpan) }) {
                        ContinueCard(lastPlayed, onResume = { onPlay(lastPlayed, LaunchOptions()) })
                    }
                }
                item(span = { GridItemSpan(maxLineSpan) }) {
                    FilterRow(filter, onChange = { filter = it })
                }
                if (shown.isEmpty()) {
                    item(span = { GridItemSpan(maxLineSpan) }) {
                        Text(
                            stringResource(if (filter == LibraryFilter.FAVORITES) R.string.no_favorites else R.string.no_matches),
                            style = MaterialTheme.typography.bodyLarge,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            modifier = Modifier.padding(vertical = 32.dp),
                        )
                    }
                }
                items(shown, key = { it.uri.toString() }) { game ->
                    GameCard(
                        game,
                        onClick = { onPlay(game, LaunchOptions()) },
                        onDetails = { detailsFor = game },
                        modifier = Modifier.animateItem(),
                    )
                }
            }
        }
    }

    if (showAddSheet) {
        AddGamesSheet(
            onDismiss = { showAddSheet = false },
            onAddFolder = { showAddSheet = false; onAddFolder() },
            onAddFile = { showAddSheet = false; onAddFile() },
        )
    }
    detailsFor?.let { selected ->
        // Re-read so favourite toggles show immediately.
        val game = Library.find(selected.uri) ?: return@let
        GameDetailsSheet(
            game,
            onDismiss = { detailsFor = null },
            onPlay = { options -> detailsFor = null; onPlay(game, options) },
            onPinShortcut = { onPinShortcut(game) },
            onRemove = { detailsFor = null; Library.hide(game.uri) },
        )
    }
}

@Composable
private fun Header(count: Int, searching: Boolean, onSearch: () -> Unit, onOpenSettings: () -> Unit) {
    val context = LocalContext.current
    Row(
        verticalAlignment = Alignment.CenterVertically,
        modifier = Modifier.fillMaxWidth().padding(top = 12.dp, bottom = 4.dp),
    ) {
        Column(Modifier.weight(1f)) {
            Text(stringResource(R.string.library), style = MaterialTheme.typography.headlineLarge)
            Text(
                context.resources.getQuantityString(R.plurals.game_count, count, count),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        IconButton(onClick = onSearch) {
            Icon(if (searching) Icons.Rounded.Close else Icons.Rounded.Search, stringResource(R.string.search))
        }
        IconButton(onClick = onOpenSettings) {
            Icon(Icons.Rounded.Settings, stringResource(R.string.settings))
        }
    }
}

@Composable
private fun SearchField(query: String, onChange: (String) -> Unit) {
    TextField(
        value = query,
        onValueChange = onChange,
        placeholder = { Text(stringResource(R.string.search_hint)) },
        leadingIcon = { Icon(Icons.Rounded.Search, null) },
        singleLine = true,
        shape = CircleShape,
        colors = TextFieldDefaults.colors(
            focusedIndicatorColor = Color.Transparent,
            unfocusedIndicatorColor = Color.Transparent,
            focusedContainerColor = MaterialTheme.colorScheme.surfaceContainerHigh,
            unfocusedContainerColor = MaterialTheme.colorScheme.surfaceContainerHigh,
        ),
        modifier = Modifier.fillMaxWidth(),
    )
}

@Composable
private fun ContinueCard(game: Game, onResume: () -> Unit) {
    val context = LocalContext.current
    Surface(
        shape = MaterialTheme.shapes.large,
        color = MaterialTheme.colorScheme.surfaceContainer,
        modifier = Modifier.fillMaxWidth().widthIn(max = 720.dp),
    ) {
        Box(Modifier.fillMaxWidth().height(196.dp)) {
            GameArt(game, Modifier.fillMaxSize())
            Box(
                Modifier.fillMaxSize().background(
                    Brush.verticalGradient(
                        0f to Color.Black.copy(alpha = 0.05f),
                        0.45f to Color.Black.copy(alpha = 0.35f),
                        1f to Color.Black.copy(alpha = 0.88f),
                    )
                )
            )
            Column(Modifier.align(Alignment.BottomStart).padding(18.dp).fillMaxWidth()) {
                Text(
                    stringResource(R.string.continue_playing).uppercase(),
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.secondary,
                )
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Column(Modifier.weight(1f)) {
                        Text(
                            game.title,
                            style = MaterialTheme.typography.titleLarge,
                            color = Color.White,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis,
                        )
                        Text(
                            "${relativeTime(context, game.lastPlayed)} · ${playTime(context, game.playTimeMs)}",
                            style = MaterialTheme.typography.bodySmall,
                            color = Color.White.copy(alpha = 0.75f),
                        )
                    }
                    Spacer(Modifier.width(12.dp))
                    Button(onClick = onResume) {
                        Icon(Icons.Rounded.PlayArrow, null, Modifier.size(20.dp))
                        Spacer(Modifier.width(6.dp))
                        Text(stringResource(R.string.resume))
                    }
                }
            }
        }
    }
}

@Composable
private fun FilterRow(filter: LibraryFilter, onChange: (LibraryFilter) -> Unit) {
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.padding(top = 4.dp)) {
        listOf(
            LibraryFilter.ALL to R.string.filter_all,
            LibraryFilter.FAVORITES to R.string.filter_favorites,
            LibraryFilter.RECENT to R.string.filter_recent,
        ).forEach { (value, label) ->
            FilterChip(
                selected = filter == value,
                onClick = { onChange(value) },
                label = { Text(stringResource(label)) },
                shape = CircleShape,
            )
        }
    }
}

@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun GameCard(game: Game, onClick: () -> Unit, onDetails: () -> Unit, modifier: Modifier = Modifier) {
    val context = LocalContext.current
    Column(modifier) {
        Box(
            Modifier
                .fillMaxWidth()
                .aspectRatio(3f / 2f)
                .clip(MaterialTheme.shapes.medium)
                .combinedClickable(onClick = onClick, onLongClick = onDetails)
        ) {
            GameArt(game, Modifier.fillMaxSize())
            if (game.favorite) {
                Icon(
                    Icons.Rounded.Star,
                    null,
                    tint = MaterialTheme.colorScheme.secondary,
                    modifier = Modifier
                        .align(Alignment.TopEnd)
                        .padding(6.dp)
                        .background(Color.Black.copy(alpha = 0.45f), CircleShape)
                        .padding(4.dp)
                        .size(16.dp),
                )
            }
        }
        Row(verticalAlignment = Alignment.Top, modifier = Modifier.padding(top = 8.dp)) {
            Column(Modifier.weight(1f).padding(start = 2.dp)) {
                Text(
                    game.title,
                    style = MaterialTheme.typography.titleSmall,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis,
                )
                Text(
                    if (game.lastPlayed > 0) relativeTime(context, game.lastPlayed) else stringResource(R.string.new_game),
                    style = MaterialTheme.typography.bodySmall,
                    color = if (game.lastPlayed > 0) MaterialTheme.colorScheme.onSurfaceVariant else MaterialTheme.colorScheme.primary,
                )
            }
            IconButton(onClick = onDetails, modifier = Modifier.size(32.dp)) {
                Icon(Icons.Rounded.MoreVert, stringResource(R.string.game_options), Modifier.size(20.dp))
            }
        }
    }
}

@Composable
private fun EmptyLibrary(onAddFolder: () -> Unit, onAddFile: () -> Unit, onOpenSettings: () -> Unit) {
    Box(Modifier.fillMaxSize()) {
        IconButton(onClick = onOpenSettings, modifier = Modifier.align(Alignment.TopEnd).padding(8.dp)) {
            Icon(Icons.Rounded.Settings, stringResource(R.string.settings))
        }
        Column(
            horizontalAlignment = Alignment.CenterHorizontally,
            modifier = Modifier.align(Alignment.Center).padding(32.dp).widthIn(max = 420.dp),
        ) {
            Box(
                Modifier
                    .size(96.dp)
                    .background(
                        Brush.linearGradient(listOf(Brand.Violet, MaterialTheme.colorScheme.primaryContainer)),
                        RoundedCornerShape(28.dp),
                    ),
                contentAlignment = Alignment.Center,
            ) {
                Icon(Icons.Rounded.SportsEsports, null, tint = Color.White, modifier = Modifier.size(52.dp))
            }
            Spacer(Modifier.height(24.dp))
            Text(stringResource(R.string.welcome_title), style = MaterialTheme.typography.headlineSmall)
            Spacer(Modifier.height(8.dp))
            Text(
                stringResource(R.string.welcome_body),
                style = MaterialTheme.typography.bodyLarge,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                textAlign = TextAlign.Center,
            )
            Spacer(Modifier.height(28.dp))
            Button(onClick = onAddFolder, modifier = Modifier.fillMaxWidth().height(52.dp)) {
                Icon(Icons.Rounded.CreateNewFolder, null)
                Spacer(Modifier.width(8.dp))
                Text(stringResource(R.string.add_folder))
            }
            Spacer(Modifier.height(10.dp))
            OutlinedButton(onClick = onAddFile, modifier = Modifier.fillMaxWidth().height(52.dp)) {
                Icon(Icons.AutoMirrored.Rounded.InsertDriveFile, null)
                Spacer(Modifier.width(8.dp))
                Text(stringResource(R.string.open_file))
            }
            Spacer(Modifier.height(20.dp))
            Text(
                stringResource(R.string.no_games_included),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f),
                textAlign = TextAlign.Center,
            )
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun AddGamesSheet(onDismiss: () -> Unit, onAddFolder: () -> Unit, onAddFile: () -> Unit) {
    ModalBottomSheet(onDismissRequest = onDismiss, containerColor = MaterialTheme.colorScheme.surfaceContainerLow) {
        Column(Modifier.windowInsetsPadding(WindowInsets.navigationBars).padding(bottom = 16.dp)) {
            Text(
                stringResource(R.string.add_games),
                style = MaterialTheme.typography.titleLarge,
                modifier = Modifier.padding(horizontal = 24.dp, vertical = 8.dp),
            )
            SheetAction(Icons.Rounded.CreateNewFolder, R.string.add_folder, R.string.add_folder_hint, onAddFolder)
            SheetAction(Icons.AutoMirrored.Rounded.InsertDriveFile, R.string.open_file, R.string.open_file_hint, onAddFile)
        }
    }
}

@Composable
private fun SheetAction(icon: ImageVector, title: Int, subtitle: Int, onClick: () -> Unit) {
    ListItem(
        leadingContent = {
            Box(
                Modifier.size(44.dp).background(MaterialTheme.colorScheme.secondaryContainer, CircleShape),
                contentAlignment = Alignment.Center,
            ) { Icon(icon, null, tint = MaterialTheme.colorScheme.onSecondaryContainer) }
        },
        headlineContent = { Text(stringResource(title), style = MaterialTheme.typography.titleMedium) },
        supportingContent = { Text(stringResource(subtitle)) },
        colors = ListItemDefaults.colors(containerColor = Color.Transparent),
        modifier = Modifier.clickable(onClick = onClick).padding(horizontal = 8.dp),
    )
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun GameDetailsSheet(
    game: Game,
    onDismiss: () -> Unit,
    onPlay: (LaunchOptions) -> Unit,
    onPinShortcut: () -> Unit,
    onRemove: () -> Unit,
) {
    val context = LocalContext.current
    val slots = remember(game.id, game.lastPlayed) { SaveSlots(context, game.id) }
    val usedSlots = (1..SaveSlots.MANUAL_SLOTS).filter { slots.exists(it) }
    val hasAuto = slots.exists(SaveSlots.AUTO_SLOT)

    ModalBottomSheet(onDismissRequest = onDismiss, containerColor = MaterialTheme.colorScheme.surfaceContainerLow) {
        Column(Modifier.windowInsetsPadding(WindowInsets.navigationBars).padding(horizontal = 24.dp).padding(bottom = 24.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                GameArt(game, Modifier.width(120.dp).aspectRatio(3f / 2f).clip(MaterialTheme.shapes.small))
                Spacer(Modifier.width(16.dp))
                Column(Modifier.weight(1f)) {
                    Text(game.title, style = MaterialTheme.typography.titleLarge, maxLines = 2, overflow = TextOverflow.Ellipsis)
                    if (game.tags.isNotEmpty()) {
                        Text(
                            game.tags.joinToString(" · "),
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                }
                IconButton(onClick = { Library.setFavorite(game.uri, !game.favorite) }) {
                    Icon(
                        if (game.favorite) Icons.Rounded.Star else Icons.Rounded.StarBorder,
                        stringResource(R.string.favorite),
                        tint = if (game.favorite) MaterialTheme.colorScheme.secondary else MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            }

            Spacer(Modifier.height(20.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                Stat(stringResource(R.string.last_played), relativeTime(context, game.lastPlayed), Modifier.weight(1f))
                Stat(stringResource(R.string.play_time), playTime(context, game.playTimeMs), Modifier.weight(1f))
                if (game.size > 0) Stat(stringResource(R.string.file_size), fileSize(context, game.size), Modifier.weight(1f))
            }

            if (usedSlots.isNotEmpty()) {
                Spacer(Modifier.height(20.dp))
                Text(stringResource(R.string.save_states), style = MaterialTheme.typography.titleSmall)
                Spacer(Modifier.height(8.dp))
                LazyRow(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    items(usedSlots) { slot ->
                        SlotThumb(slots, slot, onClick = { onPlay(LaunchOptions(slot = slot)) })
                    }
                }
            }

            Spacer(Modifier.height(24.dp))
            Button(onClick = { onPlay(LaunchOptions()) }, modifier = Modifier.fillMaxWidth().height(52.dp)) {
                Icon(Icons.Rounded.PlayArrow, null)
                Spacer(Modifier.width(8.dp))
                Text(stringResource(if (hasAuto) R.string.resume else R.string.play))
            }
            if (hasAuto) {
                Spacer(Modifier.height(8.dp))
                FilledTonalButton(onClick = { onPlay(LaunchOptions(fresh = true)) }, modifier = Modifier.fillMaxWidth().height(48.dp)) {
                    Icon(Icons.Rounded.Replay, null)
                    Spacer(Modifier.width(8.dp))
                    Text(stringResource(R.string.start_from_title))
                }
            }
            Spacer(Modifier.height(8.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedButton(onClick = onPinShortcut, modifier = Modifier.weight(1f)) {
                    Icon(Icons.AutoMirrored.Rounded.AddToHomeScreen, null, Modifier.size(18.dp))
                    Spacer(Modifier.width(6.dp))
                    Text(stringResource(R.string.add_to_home), maxLines = 1, overflow = TextOverflow.Ellipsis)
                }
                OutlinedButton(onClick = onRemove, modifier = Modifier.weight(1f)) {
                    Icon(Icons.Rounded.Delete, null, Modifier.size(18.dp))
                    Spacer(Modifier.width(6.dp))
                    Text(stringResource(R.string.remove), maxLines = 1, overflow = TextOverflow.Ellipsis)
                }
            }
        }
    }
}

@Composable
private fun Stat(label: String, value: String, modifier: Modifier = Modifier) {
    Column(
        modifier
            .background(MaterialTheme.colorScheme.surfaceContainerHigh, MaterialTheme.shapes.small)
            .padding(horizontal = 12.dp, vertical = 10.dp)
    ) {
        Text(label, style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        Text(value, style = MaterialTheme.typography.bodyMedium, maxLines = 1, overflow = TextOverflow.Ellipsis)
    }
}

@Composable
fun SlotThumb(slots: SaveSlots, slot: Int, onClick: () -> Unit) {
    val context = LocalContext.current
    val shot = rememberThumbnail(slots.thumbnail(slot).takeIf { it.exists() })
    Column(Modifier.width(112.dp)) {
        Box(
            Modifier
                .fillMaxWidth()
                .aspectRatio(3f / 2f)
                .clip(MaterialTheme.shapes.small)
                .background(MaterialTheme.colorScheme.surfaceContainerHighest)
                .clickable(onClick = onClick),
        ) {
            if (shot != null) {
                Image(
                    shot, null,
                    contentScale = ContentScale.Crop,
                    filterQuality = FilterQuality.None,
                    modifier = Modifier.fillMaxSize(),
                )
            }
        }
        Text(
            stringResource(R.string.slot_n, slot),
            style = MaterialTheme.typography.labelMedium,
            modifier = Modifier.padding(top = 4.dp),
        )
        Text(
            relativeTime(context, slots.savedAt(slot)),
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            maxLines = 1,
        )
    }
}
