package com.mulator.app

import android.content.Context
import android.graphics.Color
import android.view.View
import android.view.ViewGroup

/**
 * Places the 3:2 game screen (top in portrait, centred in landscape) and
 * stretches the controller and the menu overlay over the whole window.
 */
class EmulatorLayout(
    context: Context,
    private val screen: View,
    private val controls: ControllerView,
    private val overlay: View,
) : ViewGroup(context) {

    init {
        setBackgroundColor(Color.BLACK)
        addView(screen)
        addView(controls)
        addView(overlay)
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val w = MeasureSpec.getSize(widthMeasureSpec)
        val h = MeasureSpec.getSize(heightMeasureSpec)
        setMeasuredDimension(w, h)
        computeScreenRect(w, h)
        val r = controls.screenRect
        screen.measure(exactly(r.width()), exactly(r.height()))
        controls.measure(exactly(w), exactly(h))
        overlay.measure(exactly(w), exactly(h))
    }

    override fun onLayout(changed: Boolean, l: Int, t: Int, r: Int, b: Int) {
        val rect = controls.screenRect
        screen.layout(rect.left, rect.top, rect.right, rect.bottom)
        controls.layout(0, 0, r - l, b - t)
        overlay.layout(0, 0, r - l, b - t)
    }

    private fun computeScreenRect(w: Int, h: Int) {
        val portrait = h > w
        controls.portrait = portrait
        var sw: Int
        var sh: Int
        if (portrait) {
            sw = w
            sh = w * 2 / 3
            // Leave at least ~45% of the height for the controls.
            if (sh > h * 55 / 100) { sh = h * 55 / 100; sw = sh * 3 / 2 }
            val left = (w - sw) / 2
            val top = (resources.displayMetrics.density * 24).toInt()
            controls.screenRect.set(left, top, left + sw, top + sh)
        } else {
            sh = h
            sw = h * 3 / 2
            if (sw > w) { sw = w; sh = w * 2 / 3 }
            val left = (w - sw) / 2
            val top = (h - sh) / 2
            controls.screenRect.set(left, top, left + sw, top + sh)
        }
    }

    private fun exactly(size: Int) = View.MeasureSpec.makeMeasureSpec(size, View.MeasureSpec.EXACTLY)
}
