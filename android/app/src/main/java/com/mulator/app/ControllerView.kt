package com.mulator.app

import android.content.Context
import android.graphics.Canvas
import android.graphics.DashPathEffect
import android.graphics.Paint
import android.graphics.Rect
import android.graphics.RectF
import android.graphics.Typeface
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View
import com.mulator.app.data.Control
import com.mulator.app.data.ControllerLayout
import com.mulator.app.data.ControllerSkin
import com.mulator.app.data.ControllerSkins
import com.mulator.app.data.Placement
import java.util.EnumMap
import kotlin.math.atan2
import kotlin.math.hypot
import kotlin.math.min

/**
 * Transparent multi-touch gamepad drawn over (landscape) or under (portrait)
 * the game screen. Reports the pressed console keys as a bitmask.
 *
 * Every control has a default position; a custom [ControllerLayout] per
 * orientation can move and resize any of them. In [editing] mode touches drag
 * and pinch-resize controls instead of pressing them.
 */
class ControllerView(context: Context) : View(context) {

    var onKeysChanged: ((Int) -> Unit)? = null
    var onMenu: (() -> Unit)? = null
    var onFastForward: (() -> Unit)? = null
    /** Called with true while the rewind button is held. */
    var onRewind: ((Boolean) -> Unit)? = null

    var haptics = true
    /** False for static previews (e.g. the style picker): touches pass through. */
    var interactive = true
    /** Multiplies the size of every control (0.75 - 1.3). */
    var controlScale = 1f
        set(value) { field = value; requestLayout() }
    var showRewind = true
        set(value) { field = value; requestLayout(); invalidate() }
    var skin: ControllerSkin = ControllerSkins.Classic
        set(value) { field = value; updatePaints(); invalidate() }

    var fastForwardOn = false
        set(value) { field = value; invalidate() }

    /** Where the game screen is; set by [EmulatorLayout] before layout. */
    val screenRect = Rect()
    var portrait = true

    /** Custom layouts; null means the default layout for that orientation. */
    var portraitLayout: ControllerLayout? = null
        set(value) { field = value; requestLayout(); invalidate() }
    var landscapeLayout: ControllerLayout? = null
        set(value) { field = value; requestLayout(); invalidate() }

    // ---------------------------------------------------------------- editor API
    var editing = false
        set(value) { field = value; releaseAll(); select(null); invalidate() }
    var selected: Control? = null
        private set
    var onSelectionChanged: ((Control?) -> Unit)? = null
    /** Called after any move or resize in edit mode. */
    var onEdited: (() -> Unit)? = null
    /** Orientations whose layout was changed in this editing session. */
    val editedOrientations = HashSet<Boolean>()

    private val density = resources.displayMetrics.density
    private var u = density // layout unit, density scaled down on small screens
    private var pressed = 0
    private val heldSpecial = HashSet<Int>()
    private var rewindPointer = -1

    /** Default positions for the current size and orientation. */
    private val defaults = EnumMap<Control, RectF>(Control::class.java)
    /** Actual positions after applying the custom layout. */
    private val bounds = EnumMap<Control, RectF>(Control::class.java)

    private val visibleControls get() = Control.entries.filter { it != Control.REWIND || showRewind }

    private val fill = Paint(Paint.ANTI_ALIAS_FLAG)
    private val overlay = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val glow = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        textAlign = Paint.Align.CENTER
        typeface = Typeface.DEFAULT_BOLD
    }
    private val editOutline = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        color = 0x80FFFFFF.toInt()
        strokeWidth = 1.5f * density
        pathEffect = DashPathEffect(floatArrayOf(6 * density, 5 * density), 0f)
    }
    private val editSelected = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        color = 0xFFFFD54F.toInt()
        strokeWidth = 2.5f * density
    }

    init {
        Control.entries.forEach { defaults[it] = RectF(); bounds[it] = RectF() }
        updatePaints()
    }

    private fun updatePaints() {
        stroke.color = skin.stroke
        stroke.strokeWidth = skin.strokeDp * density
        glow.color = skin.stroke
        glow.alpha = 60
        glow.strokeWidth = (skin.strokeDp + 5f) * density
    }

    // ---------------------------------------------------------------- layout
    override fun onLayout(changed: Boolean, left: Int, top: Int, right: Int, bottom: Int) {
        super.onLayout(changed, left, top, right, bottom)
        layoutControls(width.toFloat(), height.toFloat())
    }

    private val currentLayout get() = if (portrait) portraitLayout else landscapeLayout

    private fun layoutControls(w: Float, h: Float) {
        if (w <= 0f || h <= 0f) return
        u = density * min(1f, min(w, h) / (360f * density)) * controlScale
        layoutDefaults(w, h)
        val custom = currentLayout
        for (c in Control.entries) {
            val def = defaults.getValue(c)
            val p = custom?.get(c)
            if (p == null) {
                bounds.getValue(c).set(def)
            } else {
                place(c, p.x * w, p.y * h, def.width() * p.scale, def.height() * p.scale)
            }
        }
    }

    /** Puts [c] centred on (cx, cy), kept fully on screen. */
    private fun place(c: Control, cx: Float, cy: Float, bw: Float, bh: Float) {
        val x = cx.coerceIn(bw / 2, (width - bw / 2).coerceAtLeast(bw / 2))
        val y = cy.coerceIn(bh / 2, (height - bh / 2).coerceAtLeast(bh / 2))
        bounds.getValue(c).set(x - bw / 2, y - bh / 2, x + bw / 2, y + bh / 2)
    }

    private fun layoutDefaults(w: Float, h: Float) {
        fun circle(c: Control, cx: Float, cy: Float, r: Float) = defaults.getValue(c).set(cx - r, cy - r, cx + r, cy + r)
        fun rect(c: Control, l: Float, t: Float, r: Float, b: Float) = defaults.getValue(c).set(l, t, r, b)

        if (portrait) {
            val top = screenRect.bottom.toFloat()
            val shoulderTop = top + 14 * u
            val shoulder = shoulderWidth(w)
            rect(Control.L, 16 * u, shoulderTop, 16 * u + shoulder, shoulderTop + 44 * u)
            rect(Control.R, w - 16 * u - shoulder, shoulderTop, w - 16 * u, shoulderTop + 44 * u)
            layoutSpecials(w / 2, shoulderTop + 4 * u)

            val bottomRow = h - 28 * u - 34 * u
            rect(Control.SELECT, w / 2 - 104 * u, bottomRow, w / 2 - 12 * u, bottomRow + 34 * u)
            rect(Control.START, w / 2 + 12 * u, bottomRow, w / 2 + 104 * u, bottomRow + 34 * u)

            val midTop = shoulderTop + 44 * u
            val cy = (midTop + bottomRow) / 2
            val r = min(84 * u, (bottomRow - midTop) / 2 - 6 * u).coerceAtLeast(40 * u)
            circle(Control.DPAD, 20 * u + r, cy, r)
            val br = r * 0.47f
            val ax = w - 20 * u - br
            circle(Control.A, ax, cy - br * 0.65f, br)
            circle(Control.B, ax - br * 2.3f, cy + br * 0.65f, br)
        } else {
            val shoulder = shoulderWidth(w)
            rect(Control.L, 16 * u, 14 * u, 16 * u + shoulder, 58 * u)
            rect(Control.R, w - 16 * u - shoulder, 14 * u, w - 16 * u, 58 * u)
            layoutSpecials(w / 2, 8 * u)

            val r = min(78 * u, h * 0.24f)
            circle(Control.DPAD, 28 * u + r, h * 0.56f, r)
            val br = r * 0.5f
            val ax = w - 28 * u - br
            circle(Control.A, ax, h * 0.56f - br * 0.7f, br)
            circle(Control.B, ax - br * 2.3f, h * 0.56f + br * 0.7f, br)

            val y = h - 18 * u - 32 * u
            rect(Control.SELECT, 24 * u, y, 112 * u, y + 32 * u)
            rect(Control.START, w - 112 * u, y, w - 24 * u, y + 32 * u)
        }
    }

    private fun specialsHalfWidth() = (if (showRewind) 76 else 60) * u

    /** L/R are as wide as possible (up to 96 units) without touching the centre pills. */
    private fun shoulderWidth(w: Float) = min(96 * u, w / 2 - specialsHalfWidth() - 10 * u - 16 * u).coerceAtLeast(40 * u)

    /** Menu / fast-forward (and rewind) pills, centred on [cx]. */
    private fun layoutSpecials(cx: Float, top: Float) {
        val bottom = top + 36 * u
        if (showRewind) {
            defaults.getValue(Control.REWIND).set(cx - 76 * u, top, cx - 28 * u, bottom)
            defaults.getValue(Control.MENU).set(cx - 22 * u, top, cx + 22 * u, bottom)
            defaults.getValue(Control.FAST).set(cx + 28 * u, top, cx + 76 * u, bottom)
        } else {
            defaults.getValue(Control.REWIND).set(cx - 76 * u, top, cx - 28 * u, bottom)
            defaults.getValue(Control.MENU).set(cx - 60 * u, top, cx - 6 * u, bottom)
            defaults.getValue(Control.FAST).set(cx + 6 * u, top, cx + 60 * u, bottom)
        }
    }

    // ---------------------------------------------------------------- drawing
    override fun onDraw(canvas: Canvas) {
        for (c in visibleControls) {
            val b = bounds.getValue(c)
            when (c.shape) {
                Control.Shape.DPAD -> drawDpad(canvas, b)
                Control.Shape.ROUND -> drawRound(canvas, c, b)
                Control.Shape.PILL -> drawPill(canvas, c, b)
            }
            if (editing) {
                val r = 8 * density
                canvas.drawRoundRect(b, r, r, if (c == selected) editSelected else editOutline)
            }
        }
    }

    private fun isActive(c: Control) = when (c) {
        Control.FAST -> fastForwardOn
        Control.REWIND -> rewindPointer != -1
        else -> false
    }

    private fun isPressed(c: Control) = if (c.isGameKey) pressed and c.key != 0 else c.key in heldSpecial

    private fun drawRound(canvas: Canvas, c: Control, b: RectF) {
        val cx = b.centerX(); val cy = b.centerY(); val r = b.width() / 2
        fill.color = if (c == Control.A) skin.aFill else skin.bFill
        canvas.drawCircle(cx, cy, r, fill)
        if (isPressed(c)) canvas.drawCircle(cx, cy, r, overlay.apply { color = skin.pressed })
        outline { canvas.drawCircle(cx, cy, r, it) }
        text.color = skin.faceText
        text.textSize = r * 0.56f
        canvas.drawText(c.label, cx, cy - (text.ascent() + text.descent()) / 2, text)
    }

    private fun drawPill(canvas: Canvas, c: Control, b: RectF) {
        val radius = b.height() / 2
        fill.color = if (isActive(c)) skin.accent else skin.pillFill
        canvas.drawRoundRect(b, radius, radius, fill)
        if (isPressed(c)) canvas.drawRoundRect(b, radius, radius, overlay.apply { color = skin.pressed })
        outline { canvas.drawRoundRect(b, radius, radius, it) }
        text.color = skin.pillText
        text.textSize = b.height() * if (c.label.length > 2) 0.35f else 0.5f
        canvas.drawText(c.label, b.centerX(), b.centerY() - (text.ascent() + text.descent()) / 2, text)
    }

    private fun drawDpad(canvas: Canvas, b: RectF) {
        val cx = b.centerX(); val cy = b.centerY(); val r = b.width() / 2; val t = r / 3
        val corner = t * 0.4f
        fill.color = skin.dpadFill
        overlay.color = skin.pressed
        canvas.drawRoundRect(cx - r, cy - t, cx + r, cy + t, corner, corner, fill)
        canvas.drawRoundRect(cx - t, cy - r, cx + t, cy + r, corner, corner, fill)
        if (pressed and NativeBridge.KEY_LEFT != 0) canvas.drawRoundRect(cx - r, cy - t, cx - t, cy + t, corner, corner, overlay)
        if (pressed and NativeBridge.KEY_RIGHT != 0) canvas.drawRoundRect(cx + t, cy - t, cx + r, cy + t, corner, corner, overlay)
        if (pressed and NativeBridge.KEY_UP != 0) canvas.drawRoundRect(cx - t, cy - r, cx + t, cy - t, corner, corner, overlay)
        if (pressed and NativeBridge.KEY_DOWN != 0) canvas.drawRoundRect(cx - t, cy + t, cx + t, cy + r, corner, corner, overlay)
        outline {
            canvas.drawRoundRect(cx - r, cy - t, cx + r, cy + t, corner, corner, it)
            canvas.drawRoundRect(cx - t, cy - r, cx + t, cy + r, corner, corner, it)
        }
    }

    private inline fun outline(draw: (Paint) -> Unit) {
        if (skin.strokeDp <= 0f) return
        if (skin.glow) draw(glow)
        draw(stroke)
    }

    // ---------------------------------------------------------------- input
    override fun onTouchEvent(e: MotionEvent): Boolean {
        if (!interactive) return false
        if (editing) return onEditTouch(e)
        val action = e.actionMasked
        if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
            val i = e.actionIndex
            for (c in visibleControls) {
                if (c.isGameKey || c == Control.DPAD || !hitPill(c, e.getX(i), e.getY(i))) continue
                heldSpecial.add(c.key)
                buzz()
                when (c) {
                    Control.MENU -> onMenu?.invoke()
                    Control.FAST -> onFastForward?.invoke()
                    Control.REWIND -> if (rewindPointer == -1) {
                        rewindPointer = e.getPointerId(i)
                        onRewind?.invoke(true)
                    }
                    else -> {}
                }
            }
        }
        val liftedId = when (action) {
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> e.getPointerId(e.actionIndex)
            else -> -1
        }
        if (rewindPointer != -1 && (liftedId == rewindPointer || action == MotionEvent.ACTION_CANCEL)) releaseRewind()
        var mask = 0
        if (action != MotionEvent.ACTION_CANCEL) {
            for (i in 0 until e.pointerCount) {
                val lifted = (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) &&
                    i == e.actionIndex
                if (!lifted) mask = mask or hitTest(e.getX(i), e.getY(i))
            }
        }
        if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) heldSpecial.clear()
        updatePressed(mask)
        return true
    }

    /** Clears touch state, e.g. when the pause menu opens mid-press. */
    fun releaseAll() {
        heldSpecial.clear()
        releaseRewind()
        updatePressed(0)
    }

    private fun releaseRewind() {
        if (rewindPointer == -1) return
        rewindPointer = -1
        onRewind?.invoke(false)
        invalidate()
    }

    private fun buzz() {
        if (haptics) performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY)
    }

    private fun updatePressed(mask: Int) {
        if (mask == pressed) {
            invalidate()
            return
        }
        if (mask and pressed.inv() != 0) buzz()
        pressed = mask
        onKeysChanged?.invoke(mask)
        invalidate()
    }

    private fun hitTest(x: Float, y: Float): Int {
        var mask = 0
        val pad = bounds.getValue(Control.DPAD)
        val r = pad.width() / 2
        val dx = x - pad.centerX()
        val dy = y - pad.centerY()
        val dist = hypot(dx, dy)
        if (dist <= r * 1.35f && dist >= r * 0.15f) {
            val angle = Math.toDegrees(atan2(dy, dx).toDouble())
            if (angle > -67.5 && angle < 67.5) mask = mask or NativeBridge.KEY_RIGHT
            if (angle > 112.5 || angle < -112.5) mask = mask or NativeBridge.KEY_LEFT
            if (angle > 22.5 && angle < 157.5) mask = mask or NativeBridge.KEY_DOWN
            if (angle < -22.5 && angle > -157.5) mask = mask or NativeBridge.KEY_UP
        }
        for (c in visibleControls) {
            if (!c.isGameKey) continue
            val b = bounds.getValue(c)
            val hit = if (c.shape == Control.Shape.ROUND) hypot(x - b.centerX(), y - b.centerY()) <= b.width() / 2 * 1.3f
            else hitPill(c, x, y)
            if (hit) mask = mask or c.key
        }
        return mask
    }

    private fun hitPill(c: Control, x: Float, y: Float): Boolean {
        val p = bounds.getValue(c)
        val slop = 8 * u
        return x >= p.left - slop && x <= p.right + slop && y >= p.top - slop && y <= p.bottom + slop
    }

    // ---------------------------------------------------------------- editing
    private var dragPointer = -1
    private var dragDx = 0f
    private var dragDy = 0f
    private var pinchStartDist = 0f
    private var pinchStartScale = 1f

    private fun onEditTouch(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                val x = e.x; val y = e.y
                val hit = visibleControls.lastOrNull { c ->
                    val b = bounds.getValue(c)
                    x >= b.left - 10 * density && x <= b.right + 10 * density && y >= b.top - 10 * density && y <= b.bottom + 10 * density
                }
                select(hit)
                if (hit != null) {
                    val b = bounds.getValue(hit)
                    dragPointer = e.getPointerId(0)
                    dragDx = b.centerX() - x
                    dragDy = b.centerY() - y
                }
            }
            MotionEvent.ACTION_POINTER_DOWN -> if (selected != null && e.pointerCount == 2) {
                pinchStartDist = hypot(e.getX(0) - e.getX(1), e.getY(0) - e.getY(1))
                pinchStartScale = selectedScale
            }
            MotionEvent.ACTION_MOVE -> {
                val c = selected ?: return true
                if (e.pointerCount >= 2 && pinchStartDist > 0f) {
                    val dist = hypot(e.getX(0) - e.getX(1), e.getY(0) - e.getY(1))
                    selectedScale = pinchStartScale * dist / pinchStartDist
                } else {
                    val i = e.findPointerIndex(dragPointer)
                    if (i < 0) return true
                    val b = bounds.getValue(c)
                    updatePlacement(c, e.getX(i) + dragDx, e.getY(i) + dragDy, b.width() / defaults.getValue(c).width())
                }
            }
            MotionEvent.ACTION_POINTER_UP -> {
                pinchStartDist = 0f
                // Keep dragging with whichever finger is left without a jump.
                val remaining = (0 until e.pointerCount).firstOrNull { it != e.actionIndex }
                val c = selected
                if (remaining != null && c != null) {
                    val b = bounds.getValue(c)
                    dragPointer = e.getPointerId(remaining)
                    dragDx = b.centerX() - e.getX(remaining)
                    dragDy = b.centerY() - e.getY(remaining)
                }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                dragPointer = -1
                pinchStartDist = 0f
            }
        }
        return true
    }

    private fun select(c: Control?) {
        if (selected == c) return
        selected = c
        if (c != null) buzz()
        onSelectionChanged?.invoke(c)
        invalidate()
    }

    /** Size of the selected control relative to its default (0.5 - 2). */
    var selectedScale: Float
        get() {
            val c = selected ?: return 1f
            return bounds.getValue(c).width() / defaults.getValue(c).width()
        }
        set(value) {
            val c = selected ?: return
            val b = bounds.getValue(c)
            updatePlacement(c, b.centerX(), b.centerY(), value)
        }

    fun resetSelected() {
        val c = selected ?: return
        val def = defaults.getValue(c)
        updatePlacement(c, def.centerX(), def.centerY(), 1f)
    }

    /** Back to the default layout for the current orientation. */
    fun resetCurrentOrientation() {
        if (portrait) portraitLayout = null else landscapeLayout = null
        editedOrientations += portrait
        select(null)
        onEdited?.invoke()
    }

    private fun updatePlacement(c: Control, cx: Float, cy: Float, scale: Float) {
        val w = width.toFloat(); val h = height.toFloat()
        if (w <= 0f || h <= 0f) return
        // The first edit turns the default layout into a full custom snapshot.
        val layout = (currentLayout ?: Control.entries.associateWith { snapshot(it) }).toMutableMap()
        val def = defaults.getValue(c)
        val s = scale.coerceIn(MIN_SCALE, MAX_SCALE)
        place(c, cx, cy, def.width() * s, def.height() * s)
        layout[c] = snapshot(c)
        if (portrait) portraitLayout = layout else landscapeLayout = layout
        editedOrientations += portrait
        onEdited?.invoke()
    }

    private fun snapshot(c: Control): Placement {
        val b = bounds.getValue(c)
        return Placement(b.centerX() / width, b.centerY() / height, b.width() / defaults.getValue(c).width())
    }

    companion object {
        const val MIN_SCALE = 0.5f
        const val MAX_SCALE = 2f
    }
}
