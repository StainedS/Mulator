package com.mulator.app.data

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.provider.DocumentsContract
import android.provider.OpenableColumns
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

/** File extensions the app can run. */
val SUPPORTED_EXTENSIONS = setOf("gba")

data class Game(
    val uri: Uri,
    val fileName: String,
    val size: Long = 0,
    /** Tree URI of the library folder this game was found in, or null if opened as a single file. */
    val folder: Uri? = null,
    val addedAt: Long = System.currentTimeMillis(),
    val lastPlayed: Long = 0,
    val playTimeMs: Long = 0,
    val favorite: Boolean = false,
    /** Removed by the user; kept so folder rescans don't bring it back. */
    val hidden: Boolean = false,
) {
    /** Stable key for save files; matches the naming used before the library existed. */
    val id: String get() = gameId(fileName)

    /** "Star Quest (v1.2) [!].ext" -> "Star Quest" */
    val title: String
        get() = fileName.substringBeforeLast('.')
            .replace(TAG_REGEX, "")
            .replace('_', ' ')
            .trim()
            .ifBlank { fileName }

    /** "Star Quest (v1.2) [!].ext" -> ["v1.2", "!"] */
    val tags: List<String>
        get() = TAG_REGEX.findAll(fileName.substringBeforeLast('.')).map { it.value.trim('(', ')', '[', ']', ' ') }.toList()

    val extension: String get() = fileName.substringAfterLast('.', "").lowercase()

    private companion object {
        val TAG_REGEX = Regex("""\s*[(\[][^)\]]*[)\]]""")
    }
}

fun gameId(fileName: String): String =
    fileName.substringBeforeLast('.').replace(Regex("[^A-Za-z0-9 ._()-]"), "_").ifBlank { "game" }

/**
 * The user's game collection: library folders (persisted SAF tree grants) plus
 * individually opened files, with play statistics. Backed by a JSON file and
 * exposed as Compose state, so every screen in the process sees the same data.
 */
object Library {
    private const val FILE = "library.json"
    private const val MAX_SCAN_DEPTH = 6

    var games by mutableStateOf<List<Game>>(emptyList())
        private set
    var folders by mutableStateOf<List<Uri>>(emptyList())
        private set

    private var loaded = false
    private lateinit var appContext: Context

    fun init(context: Context) {
        if (loaded) return
        appContext = context.applicationContext
        loaded = true
        val file = File(appContext.filesDir, FILE)
        if (file.exists()) {
            try {
                val root = JSONObject(file.readText())
                folders = root.optJSONArray("folders").strings().map(Uri::parse)
                games = root.optJSONArray("games").objects().map(::gameFromJson)
            } catch (e: Exception) {
                games = emptyList()
            }
        } else {
            migrateRecents()
        }
    }

    val visibleGames: List<Game> get() = games.filter { !it.hidden }

    fun find(uri: Uri): Game? = games.firstOrNull { it.uri == uri }

    /** Adds a single file (from the system picker) and returns its entry. */
    fun addFile(context: Context, uri: Uri): Game {
        find(uri)?.let { existing ->
            if (existing.hidden) update(uri) { it.copy(hidden = false) }
            return find(uri)!!
        }
        val game = Game(uri = uri, fileName = displayName(context, uri), size = fileSize(context, uri))
        games = games + game
        persist()
        return game
    }

    suspend fun addFolder(context: Context, tree: Uri) {
        try {
            context.contentResolver.takePersistableUriPermission(tree, Intent.FLAG_GRANT_READ_URI_PERMISSION)
        } catch (e: SecurityException) {
            // Without a persistable grant the folder only works until the app restarts.
        }
        if (tree !in folders) folders = folders + tree
        persist()
        rescan(context)
    }

    fun removeFolder(context: Context, tree: Uri) {
        folders = folders - tree
        games = games.filter { it.folder != tree }
        try {
            context.contentResolver.releasePersistableUriPermission(tree, Intent.FLAG_GRANT_READ_URI_PERMISSION)
        } catch (e: SecurityException) {
        }
        persist()
    }

    /** Walks every library folder and syncs the game list with what is on disk. */
    suspend fun rescan(context: Context) {
        val found = withContext(Dispatchers.IO) {
            folders.associateWith { tree ->
                try {
                    val out = ArrayList<Game>()
                    scan(context, tree, DocumentsContract.getTreeDocumentId(tree), 0, out)
                    out
                } catch (e: Exception) {
                    null // permission lost or provider gone: leave this folder's games alone
                }
            }
        }
        var list = games
        for ((tree, scanned) in found) {
            if (scanned == null) continue
            val existing = list.filter { it.folder == tree }.associateBy { it.uri }
            val merged = scanned.map { g -> existing[g.uri]?.copy(fileName = g.fileName, size = g.size) ?: g }
            list = list.filter { it.folder != tree } + merged
        }
        games = list
        persist()
    }

    private fun scan(context: Context, tree: Uri, parentId: String, depth: Int, out: MutableList<Game>) {
        val children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, parentId)
        val cols = arrayOf(
            DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_MIME_TYPE,
            DocumentsContract.Document.COLUMN_SIZE,
        )
        context.contentResolver.query(children, cols, null, null, null)?.use { c ->
            while (c.moveToNext()) {
                val docId = c.getString(0) ?: continue
                val name = c.getString(1) ?: continue
                val mime = c.getString(2)
                if (mime == DocumentsContract.Document.MIME_TYPE_DIR) {
                    if (depth < MAX_SCAN_DEPTH && !name.startsWith(".")) scan(context, tree, docId, depth + 1, out)
                } else if (name.substringAfterLast('.', "").lowercase() in SUPPORTED_EXTENSIONS) {
                    val uri = DocumentsContract.buildDocumentUriUsingTree(tree, docId)
                    out += Game(uri = uri, fileName = name, size = if (c.isNull(3)) 0 else c.getLong(3), folder = tree)
                }
            }
        }
    }

    fun setFavorite(uri: Uri, favorite: Boolean) = update(uri) { it.copy(favorite = favorite) }

    fun hide(uri: Uri) {
        val game = find(uri) ?: return
        games = if (game.folder != null) games.map { if (it.uri == uri) it.copy(hidden = true) else it }
        else games.filter { it.uri != uri }
        persist()
    }

    fun recordSession(uri: Uri, playedMs: Long) =
        update(uri) { it.copy(lastPlayed = System.currentTimeMillis(), playTimeMs = it.playTimeMs + playedMs.coerceAtLeast(0)) }

    private fun update(uri: Uri, change: (Game) -> Game) {
        if (games.none { it.uri == uri }) return
        games = games.map { if (it.uri == uri) change(it) else it }
        persist()
    }

    // ------------------------------------------------------------ persistence
    private fun persist() {
        val root = JSONObject()
            .put("folders", JSONArray(folders.map { it.toString() }))
            .put("games", JSONArray(games.map(::gameToJson)))
        val file = File(appContext.filesDir, FILE)
        val tmp = File(appContext.filesDir, "$FILE.tmp")
        tmp.writeText(root.toString())
        tmp.renameTo(file)
    }

    private fun gameToJson(g: Game) = JSONObject()
        .put("uri", g.uri.toString())
        .put("name", g.fileName)
        .put("size", g.size)
        .put("folder", g.folder?.toString())
        .put("added", g.addedAt)
        .put("played", g.lastPlayed)
        .put("playtime", g.playTimeMs)
        .put("fav", g.favorite)
        .put("hidden", g.hidden)

    private fun gameFromJson(o: JSONObject) = Game(
        uri = Uri.parse(o.getString("uri")),
        fileName = o.getString("name"),
        size = o.optLong("size"),
        folder = o.optString("folder").takeIf { it.isNotEmpty() && it != "null" }?.let(Uri::parse),
        addedAt = o.optLong("added"),
        lastPlayed = o.optLong("played"),
        playTimeMs = o.optLong("playtime"),
        favorite = o.optBoolean("fav"),
        hidden = o.optBoolean("hidden"),
    )

    /** Version 0.1 kept a "recent ROMs" list in shared preferences. */
    private fun migrateRecents() {
        val prefs = appContext.getSharedPreferences("recent_roms", Context.MODE_PRIVATE)
        val raw = prefs.getString("list", null) ?: return
        try {
            games = JSONArray(raw).objects().map {
                Game(uri = Uri.parse(it.getString("uri")), fileName = it.getString("name"), lastPlayed = it.getLong("time"))
            }
            persist()
            prefs.edit().clear().apply()
        } catch (e: Exception) {
        }
    }

    private fun JSONArray?.strings(): List<String> = if (this == null) emptyList() else (0 until length()).map { getString(it) }
    private fun JSONArray?.objects(): List<JSONObject> = if (this == null) emptyList() else (0 until length()).map { getJSONObject(it) }

    fun displayName(context: Context, uri: Uri): String {
        try {
            context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
                if (c.moveToFirst()) c.getString(0)?.takeIf { it.isNotBlank() }?.let { return it }
            }
        } catch (e: Exception) {
        }
        return uri.lastPathSegment?.substringAfterLast('/') ?: "game"
    }

    private fun fileSize(context: Context, uri: Uri): Long {
        try {
            context.contentResolver.query(uri, arrayOf(OpenableColumns.SIZE), null, null, null)?.use { c ->
                if (c.moveToFirst() && !c.isNull(0)) return c.getLong(0)
            }
        } catch (e: Exception) {
        }
        return 0
    }
}
