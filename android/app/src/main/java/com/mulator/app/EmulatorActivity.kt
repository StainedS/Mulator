package com.mulator.app

import android.net.Uri
import android.os.Bundle
import android.os.SystemClock
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.WindowManager
import android.widget.Toast
import java.io.File
import androidx.activity.ComponentActivity
import androidx.activity.OnBackPressedCallback
import androidx.compose.ui.platform.ComposeView
import androidx.compose.ui.platform.ViewCompositionStrategy
import com.mulator.app.data.Library
import com.mulator.app.data.SaveSlots
import com.mulator.app.data.Settings
import com.mulator.app.data.gameId
import com.mulator.app.ui.GameMenuActions
import com.mulator.app.ui.GameOverlay
import com.mulator.app.ui.GameOverlayState
import com.mulator.app.ui.MulatorTheme
import com.mulator.app.ui.Notice

/** Runs one game: native emulation thread, touch controls, gamepad input and the pause menu. */
class EmulatorActivity : ComponentActivity(), SurfaceHolder.Callback, GameMenuActions {

    private lateinit var controls: ControllerView
    private lateinit var slots: SaveSlots
    private lateinit var gameUri: Uri
    private lateinit var title: String
    private val ui = GameOverlayState()
    private var touchKeys = 0
    private var padKeys = 0
    private var stickKeys = 0
    private var loaded = false
    private var sessionStart = 0L
    private var sessionMs = 0L

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        gameUri = intent.data ?: return finish()
        val name = intent.getStringExtra(EXTRA_NAME) ?: "game"
        slots = SaveSlots(this, gameId(name))

        val rom = try {
            contentResolver.openInputStream(gameUri)?.use { it.readBytes() }
        } catch (e: Exception) {
            null
        }
        loaded = rom != null && NativeBridge.loadRom(rom, slots.batterySave.absolutePath)
        if (!loaded) {
            Toast.makeText(this, R.string.load_failed, Toast.LENGTH_LONG).show()
            finish()
            return
        }
        title = Library.find(gameUri)?.title ?: name.substringBeforeLast('.')

        NativeBridge.setRewindEnabled(Settings.rewindEnabled)
        NativeBridge.setFastForwardSpeed(Settings.fastForwardSpeed)
        // Recreated after a configuration change or process death: the auto state
        // written in onStop is the newest one.
        restoreStartingPoint(savedInstanceState != null)

        val screen = SurfaceView(this)
        screen.holder.addCallback(this)
        controls = ControllerView(this).apply {
            onKeysChanged = { touchKeys = it; pushKeys() }
            onMenu = { openMenu(fromGamepad = false) }
            onFastForward = { toggleFastForward() }
            onRewind = { NativeBridge.setRewinding(it) }
        }
        applyControllerSettings(controls)
        val overlay = ComposeView(this).apply {
            setViewCompositionStrategy(ViewCompositionStrategy.DisposeOnViewTreeLifecycleDestroyed)
            setContent {
                MulatorTheme {
                    GameOverlay(ui, title, slots, Settings.fastForwardSpeed, this@EmulatorActivity)
                }
            }
        }
        setContentView(EmulatorLayout(this, screen, controls, overlay))
        hideSystemBars()

        onBackPressedDispatcher.addCallback(this, object : OnBackPressedCallback(true) {
            override fun handleOnBackPressed() {
                if (ui.menuOpen) resume() else openMenu(fromGamepad = false)
            }
        })
        NativeBridge.start()
    }

    /** Decides where the game starts: a chosen slot, the auto-resume state, or a cold boot. */
    private fun restoreStartingPoint(recreated: Boolean) {
        val slot = intent.getIntExtra(EXTRA_SLOT, -1)
        val fresh = intent.getBooleanExtra(EXTRA_FRESH, false)
        when {
            recreated -> loadQuietly(SaveSlots.AUTO_SLOT)
            slot > 0 && slots.exists(slot) -> {
                if (loadQuietly(slot)) ui.notice = Notice(getString(R.string.loaded_slot, slot))
            }
            !fresh && Settings.resumeOnLaunch && slots.exists(SaveSlots.AUTO_SLOT) -> {
                if (loadQuietly(SaveSlots.AUTO_SLOT)) {
                    ui.notice = Notice(getString(R.string.resumed), getString(R.string.restart), action = NativeBridge::reset)
                }
            }
        }
    }

    private fun loadQuietly(slot: Int) = slots.exists(slot) && NativeBridge.loadState(slots.state(slot).absolutePath)

    override fun onResume() {
        super.onResume()
        if (!loaded) return
        sessionStart = SystemClock.elapsedRealtime()
        applyControllerSettings(controls) // the layout editor may have changed them
        if (!ui.menuOpen) NativeBridge.setPaused(false)
    }

    override fun onPause() {
        super.onPause()
        if (!loaded) return
        sessionMs += SystemClock.elapsedRealtime() - sessionStart
        controls.releaseAll()
        NativeBridge.setPaused(true) // also writes the battery save
    }

    override fun onStop() {
        super.onStop()
        if (!loaded) return
        // Snapshot for "resume where you left off" and the library cover.
        writeSlot(SaveSlots.AUTO_SLOT)
        Library.recordSession(gameUri, sessionMs)
        sessionMs = 0
    }

    override fun onDestroy() {
        super.onDestroy()
        if (loaded && isFinishing) NativeBridge.stop()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) hideSystemBars()
    }

    // ---------------------------------------------------------------- surface
    override fun surfaceCreated(holder: SurfaceHolder) {}

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        NativeBridge.setSurface(holder.surface, width, height)
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        NativeBridge.setSurface(null, 0, 0)
    }

    // ---------------------------------------------------------------- menu
    private fun openMenu(fromGamepad: Boolean) {
        if (ui.menuOpen) return
        controls.releaseAll()
        NativeBridge.setRewinding(false)
        NativeBridge.setPaused(true)
        ui.focusOnOpen = fromGamepad
        ui.menuOpen = true
    }

    override fun resume() {
        ui.menuOpen = false
        NativeBridge.setPaused(false)
    }

    override fun saveSlot(slot: Int) {
        val ok = writeSlot(slot)
        ui.slotsVersion++
        ui.notice = Notice(getString(if (ok) R.string.saved_slot else R.string.state_failed, slot))
    }

    override fun loadSlot(slot: Int) {
        if (loadQuietly(slot)) {
            ui.notice = Notice(
                if (slot == SaveSlots.AUTO_SLOT) getString(R.string.resumed) else getString(R.string.loaded_slot, slot)
            )
            resume()
        } else {
            ui.notice = Notice(getString(R.string.state_load_failed))
        }
    }

    override fun toggleFastForward() {
        ui.fastForward = !ui.fastForward
        NativeBridge.setFastForward(ui.fastForward)
        controls.fastForwardOn = ui.fastForward
    }

    override fun reset() {
        NativeBridge.reset()
        resume()
    }

    override fun quit() {
        finish()
    }

    override fun editControls() {
        val background = File(cacheDir, "editor_background.png")
        try {
            SaveSlots.writePng(background, NativeBridge.screenshot(), NativeBridge.SCREEN_WIDTH, NativeBridge.SCREEN_HEIGHT)
        } catch (e: Exception) {
            background.delete()
        }
        startActivity(LayoutEditorActivity.intent(this, background.takeIf { it.exists() }))
    }

    private fun writeSlot(slot: Int): Boolean {
        if (!NativeBridge.saveState(slots.state(slot).absolutePath)) return false
        try {
            slots.writeThumbnail(slot, NativeBridge.screenshot(), NativeBridge.SCREEN_WIDTH, NativeBridge.SCREEN_HEIGHT)
        } catch (e: Exception) {
            // The state itself is saved; a missing thumbnail only affects the menu.
        }
        return true
    }

    // ---------------------------------------------------------------- gamepad
    private fun pushKeys() = NativeBridge.setKeys(touchKeys or padKeys or stickKeys)

    private fun keyBit(keyCode: Int): Int = when (keyCode) {
        KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_BUTTON_X -> NativeBridge.KEY_A
        KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_BUTTON_Y -> NativeBridge.KEY_B
        KeyEvent.KEYCODE_BUTTON_L1 -> NativeBridge.KEY_L
        KeyEvent.KEYCODE_BUTTON_R1 -> NativeBridge.KEY_R
        KeyEvent.KEYCODE_BUTTON_START, KeyEvent.KEYCODE_ENTER -> NativeBridge.KEY_START
        KeyEvent.KEYCODE_BUTTON_SELECT, KeyEvent.KEYCODE_DEL -> NativeBridge.KEY_SELECT
        KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_W -> NativeBridge.KEY_UP
        KeyEvent.KEYCODE_DPAD_DOWN, KeyEvent.KEYCODE_S -> NativeBridge.KEY_DOWN
        KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_A -> NativeBridge.KEY_LEFT
        KeyEvent.KEYCODE_DPAD_RIGHT, KeyEvent.KEYCODE_D -> NativeBridge.KEY_RIGHT
        KeyEvent.KEYCODE_K -> NativeBridge.KEY_A
        KeyEvent.KEYCODE_J -> NativeBridge.KEY_B
        else -> 0
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (!loaded) return super.dispatchKeyEvent(event)
        if (ui.menuOpen) {
            // Let the menu be driven with a controller: A clicks, B / Select / Home close it.
            when (event.keyCode) {
                KeyEvent.KEYCODE_BUTTON_A -> return super.dispatchKeyEvent(
                    KeyEvent(event.downTime, event.eventTime, event.action, KeyEvent.KEYCODE_DPAD_CENTER, event.repeatCount)
                )
                KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_BUTTON_MODE, KeyEvent.KEYCODE_BUTTON_SELECT -> {
                    if (event.action == KeyEvent.ACTION_UP) resume()
                    return true
                }
            }
            return super.dispatchKeyEvent(event)
        }
        val down = event.action == KeyEvent.ACTION_DOWN
        when (event.keyCode) {
            KeyEvent.KEYCODE_BUTTON_MODE -> {
                if (down) openMenu(fromGamepad = true)
                return true
            }
            KeyEvent.KEYCODE_BUTTON_L2 -> { // hold to rewind
                if (Settings.rewindEnabled) NativeBridge.setRewinding(down)
                return true
            }
            KeyEvent.KEYCODE_BUTTON_R2 -> { // hold to fast-forward
                if (down && event.repeatCount == 0 || !down) {
                    ui.fastForward = down
                    NativeBridge.setFastForward(down)
                    controls.fastForwardOn = down
                }
                return true
            }
        }
        val bit = keyBit(event.keyCode)
        if (bit == 0) return super.dispatchKeyEvent(event)
        padKeys = if (down) padKeys or bit else padKeys and bit.inv()
        pushKeys()
        return true
    }

    override fun onGenericMotionEvent(event: MotionEvent): Boolean {
        val joystick = event.source and InputDevice.SOURCE_JOYSTICK == InputDevice.SOURCE_JOYSTICK
        if (!joystick || event.action != MotionEvent.ACTION_MOVE || ui.menuOpen) return super.onGenericMotionEvent(event)
        val x = event.getAxisValue(MotionEvent.AXIS_HAT_X) + event.getAxisValue(MotionEvent.AXIS_X)
        val y = event.getAxisValue(MotionEvent.AXIS_HAT_Y) + event.getAxisValue(MotionEvent.AXIS_Y)
        var keys = 0
        if (x < -0.5f) keys = keys or NativeBridge.KEY_LEFT
        if (x > 0.5f) keys = keys or NativeBridge.KEY_RIGHT
        if (y < -0.5f) keys = keys or NativeBridge.KEY_UP
        if (y > 0.5f) keys = keys or NativeBridge.KEY_DOWN
        stickKeys = keys
        pushKeys()
        return true
    }

    companion object {
        const val EXTRA_NAME = "name"
        /** Ignore the auto-resume state and boot the game from the start. */
        const val EXTRA_FRESH = "fresh"
        /** Load this manual save slot right after starting. */
        const val EXTRA_SLOT = "slot"
    }
}
