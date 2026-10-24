package com.mulator.app

import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Rect
import android.graphics.Typeface
import android.os.Bundle
import android.view.View
import android.view.WindowManager
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.OnBackPressedCallback
import androidx.compose.ui.platform.ComposeView
import androidx.compose.ui.platform.ViewCompositionStrategy
import com.mulator.app.data.ControllerLayouts
import com.mulator.app.data.SaveSlots
import com.mulator.app.ui.LayoutEditorActions
import com.mulator.app.ui.LayoutEditorOverlay
import com.mulator.app.ui.LayoutEditorState
import com.mulator.app.ui.MulatorTheme
import java.io.File

/**
 * Lets the player drag and resize every touch control on top of a still of
 * the game screen. Portrait and landscape layouts are edited (and saved)
 * separately: rotating the phone switches which one is being edited.
 */
class LayoutEditorActivity : ComponentActivity(), LayoutEditorActions {

    private lateinit var controls: ControllerView
    private val state = LayoutEditorState()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        val background = intent.getStringExtra(EXTRA_BACKGROUND)?.let { SaveSlots.decode(File(it)) }
        controls = ControllerView(this).apply {
            applyControllerSettings(this)
            editing = true
            onSelectionChanged = { state.selected = it; state.scale = selectedScale }
            onEdited = { state.dirty = true; state.scale = selectedScale }
        }
        val overlay = ComposeView(this).apply {
            setViewCompositionStrategy(ViewCompositionStrategy.DisposeOnViewTreeLifecycleDestroyed)
            setContent { MulatorTheme { LayoutEditorOverlay(state, this@LayoutEditorActivity) } }
        }
        setContentView(EmulatorLayout(this, ScreenPreview(this, background), controls, overlay))
        hideSystemBars()

        onBackPressedDispatcher.addCallback(this, object : OnBackPressedCallback(true) {
            override fun handleOnBackPressed() = cancel()
        })
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) hideSystemBars()
    }

    override fun setScale(scale: Float) {
        controls.selectedScale = scale
        state.scale = controls.selectedScale
    }

    override fun resetSelected() = controls.resetSelected()

    override fun resetAll() = controls.resetCurrentOrientation()

    override fun save() {
        for (portrait in controls.editedOrientations) {
            ControllerLayouts.save(portrait, if (portrait) controls.portraitLayout else controls.landscapeLayout)
        }
        Toast.makeText(this, R.string.layout_saved, Toast.LENGTH_SHORT).show()
        finish()
    }

    override fun cancel() {
        if (state.dirty) state.confirmDiscard = true else finish()
    }

    override fun discard() = finish()

    companion object {
        private const val EXTRA_BACKGROUND = "background"

        /** [background] is an optional screenshot shown where the game screen goes. */
        fun intent(context: Context, background: File?): Intent =
            Intent(context, LayoutEditorActivity::class.java).putExtra(EXTRA_BACKGROUND, background?.path)
    }
}

/** Stands in for the game screen while editing: a screenshot, or a labelled placeholder. */
private class ScreenPreview(context: Context, private val shot: Bitmap?) : View(context) {
    private val pixels = Paint().apply { isFilterBitmap = false }
    private val box = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xFF16161F.toInt() }
    private val label = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0x66FFFFFF
        textAlign = Paint.Align.CENTER
        typeface = Typeface.DEFAULT_BOLD
        textSize = 16 * resources.displayMetrics.density
    }
    private val dst = Rect()

    override fun onDraw(canvas: Canvas) {
        dst.set(0, 0, width, height)
        if (shot != null) {
            canvas.drawBitmap(shot, null, dst, pixels)
            canvas.drawColor(0x55000000) // dim it so the controls stand out
        } else {
            canvas.drawColor(Color.BLACK)
            canvas.drawRect(dst, box)
            canvas.drawText(context.getString(R.string.game_screen), width / 2f, height / 2f, label)
        }
    }
}
