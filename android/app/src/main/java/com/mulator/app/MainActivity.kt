package com.mulator.app

import android.content.Intent
import android.graphics.Color
import android.net.Uri
import android.os.Bundle
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.SystemBarStyle
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.animation.togetherWith
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.lifecycle.lifecycleScope
import com.mulator.app.data.Game
import com.mulator.app.data.Library
import com.mulator.app.data.SaveSlots
import com.mulator.app.ui.LaunchOptions
import com.mulator.app.ui.LibraryScreen
import com.mulator.app.ui.MulatorTheme
import com.mulator.app.ui.SettingsScreen
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch

/** Game library, settings, and the entry point for launcher shortcuts. */
class MainActivity : ComponentActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        // The app is always dark, so system bar icons must stay light even in light mode.
        enableEdgeToEdge(
            statusBarStyle = SystemBarStyle.dark(Color.TRANSPARENT),
            navigationBarStyle = SystemBarStyle.dark(Color.TRANSPARENT),
        )
        super.onCreate(savedInstanceState)
        if (savedInstanceState == null) handleShortcut(intent)

        setContent {
            MulatorTheme {
                val scope = rememberCoroutineScope()
                var inSettings by rememberSaveable { mutableStateOf(false) }

                val folderPicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { tree ->
                    if (tree != null) scope.launch { Library.addFolder(this@MainActivity, tree) }
                }
                val filePicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
                    if (uri != null) openFile(uri)
                }
                // Game files have no registered MIME type, so the picker has to show everything.
                val pickFile = { filePicker.launch(arrayOf("*/*")) }
                val pickFolder = { folderPicker.launch(null) }

                LaunchedEffect(Unit) { Library.rescan(this@MainActivity) }
                BackHandler(enabled = inSettings) { inSettings = false }

                AnimatedContent(
                    targetState = inSettings,
                    transitionSpec = {
                        if (targetState) (slideInHorizontally { it / 4 } + fadeIn()) togetherWith fadeOut()
                        else fadeIn() togetherWith (slideOutHorizontally { it / 4 } + fadeOut())
                    },
                    label = "screen",
                ) { settings ->
                    if (settings) {
                        SettingsScreen(onBack = { inSettings = false }, onAddFolder = pickFolder, onEditLayout = ::editLayout)
                    } else {
                        LibraryScreen(
                            onPlay = ::play,
                            onAddFolder = pickFolder,
                            onAddFile = pickFile,
                            onOpenSettings = { inSettings = true },
                            onPinShortcut = { game ->
                                if (!Shortcuts.pin(this@MainActivity, game)) {
                                    Toast.makeText(this@MainActivity, R.string.shortcut_unsupported, Toast.LENGTH_SHORT).show()
                                }
                            },
                        )
                    }
                }
            }
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        handleShortcut(intent)
    }

    override fun onResume() {
        super.onResume()
        lifecycleScope.launch(Dispatchers.IO) { Shortcuts.updateRecent(this@MainActivity) }
    }

    /** Opens the layout editor over a screenshot of the most recently played game, if any. */
    private fun editLayout() {
        val recent = Library.visibleGames.filter { it.lastPlayed > 0 }.maxByOrNull { it.lastPlayed }
        val background = recent?.let { SaveSlots(this, it.id).latestThumbnail() }
        startActivity(LayoutEditorActivity.intent(this, background))
    }

    private fun handleShortcut(intent: Intent?) {
        if (intent?.action != ACTION_PLAY) return
        val uri = intent.data ?: return
        val game = Library.find(uri) ?: Game(uri = uri, fileName = Library.displayName(this, uri))
        play(game, LaunchOptions())
    }

    private fun openFile(uri: Uri) {
        try {
            contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
        } catch (e: SecurityException) {
            // Some providers don't offer persistable grants; it still works this session.
        }
        play(Library.addFile(this, uri), LaunchOptions())
    }

    private fun play(game: Game, options: LaunchOptions) {
        startActivity(
            Intent(this, EmulatorActivity::class.java)
                .setData(game.uri)
                .putExtra(EmulatorActivity.EXTRA_NAME, game.fileName)
                .putExtra(EmulatorActivity.EXTRA_FRESH, options.fresh)
                .putExtra(EmulatorActivity.EXTRA_SLOT, options.slot ?: -1)
                .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        )
    }

    companion object {
        const val ACTION_PLAY = "com.mulator.app.action.PLAY"
    }
}
