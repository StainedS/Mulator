package com.mulator.app.data

import android.content.Context
import android.content.SharedPreferences
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.setValue
import com.mulator.app.NativeBridge
import com.mulator.app.R as Res
import org.json.JSONObject

/** Every on-screen control. Negative keys are app functions rather than console buttons. */
enum class Control(val key: Int, val label: String, val shape: Shape, val title: Int) {
    DPAD(0, "", Shape.DPAD, Res.string.control_dpad),
    A(NativeBridge.KEY_A, "A", Shape.ROUND, Res.string.control_a),
    B(NativeBridge.KEY_B, "B", Shape.ROUND, Res.string.control_b),
    L(NativeBridge.KEY_L, "L", Shape.PILL, Res.string.control_l),
    R(NativeBridge.KEY_R, "R", Shape.PILL, Res.string.control_r),
    SELECT(NativeBridge.KEY_SELECT, "SELECT", Shape.PILL, Res.string.control_select),
    START(NativeBridge.KEY_START, "START", Shape.PILL, Res.string.control_start),
    REWIND(-3, "«", Shape.PILL, Res.string.control_rewind),
    MENU(-1, "☰", Shape.PILL, Res.string.control_menu),
    FAST(-2, "»", Shape.PILL, Res.string.control_fast);

    enum class Shape { DPAD, ROUND, PILL }

    val isGameKey get() = key > 0
}

/**
 * Where a control sits in a custom layout: its centre as a fraction of the
 * controller's width and height, and its size relative to the default layout.
 */
data class Placement(val x: Float, val y: Float, val scale: Float)

typealias ControllerLayout = Map<Control, Placement>

/** Custom layouts, one for portrait and one for landscape. A missing one means the default layout. */
object ControllerLayouts {
    private const val PREFS = "controller_layouts"
    private lateinit var prefs: SharedPreferences

    /** Bumped on every change so Compose screens can react. */
    var version by mutableIntStateOf(0)
        private set

    fun init(context: Context) {
        if (::prefs.isInitialized) return
        prefs = context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
    }

    private fun key(portrait: Boolean) = if (portrait) "portrait" else "landscape"

    fun load(portrait: Boolean): ControllerLayout? {
        val raw = prefs.getString(key(portrait), null) ?: return null
        return try {
            val o = JSONObject(raw)
            Control.entries.mapNotNull { c ->
                o.optJSONObject(c.name)?.let { p ->
                    c to Placement(p.getDouble("x").toFloat(), p.getDouble("y").toFloat(), p.getDouble("s").toFloat())
                }
            }.toMap().takeIf { it.isNotEmpty() }
        } catch (e: Exception) {
            null
        }
    }

    fun save(portrait: Boolean, layout: ControllerLayout?) {
        val edit = prefs.edit()
        if (layout == null) {
            edit.remove(key(portrait))
        } else {
            val o = JSONObject()
            for ((c, p) in layout) o.put(c.name, JSONObject().put("x", p.x.toDouble()).put("y", p.y.toDouble()).put("s", p.scale.toDouble()))
            edit.putString(key(portrait), o.toString())
        }
        edit.apply()
        version++
    }

    fun hasCustom(): Boolean = prefs.contains(key(true)) || prefs.contains(key(false))
}
