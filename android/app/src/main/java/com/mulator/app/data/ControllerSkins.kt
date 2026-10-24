package com.mulator.app.data

import com.mulator.app.R

/**
 * The look of the touch controls. Colours are ARGB. [pressed] is painted on top
 * of a control's own fill while it is held.
 */
data class ControllerSkin(
    val id: String,
    val title: Int,
    val fill: Int,
    val pressed: Int,
    val stroke: Int,
    val strokeDp: Float,
    val text: Int,
    val accent: Int,
    val dpadFill: Int = fill,
    val aFill: Int = fill,
    val bFill: Int = fill,
    val faceText: Int = text,
    val pillFill: Int = fill,
    val pillText: Int = text,
    /** Soft halo around outlines. */
    val glow: Boolean = false,
)

object ControllerSkins {
    val Classic = ControllerSkin(
        id = "classic", title = R.string.skin_classic,
        fill = 0x38FFFFFF, pressed = 0x58FFFFFF, stroke = 0x70FFFFFF, strokeDp = 1.5f,
        text = 0xD0FFFFFF.toInt(), accent = 0xB07C5CFF.toInt(),
    )

    val Minimal = ControllerSkin(
        id = "minimal", title = R.string.skin_minimal,
        fill = 0x00000000, pressed = 0x40FFFFFF, stroke = 0x90FFFFFF.toInt(), strokeDp = 1f,
        text = 0xB0FFFFFF.toInt(), accent = 0x907C5CFF.toInt(),
    )

    val Console = ControllerSkin(
        id = "console", title = R.string.skin_console,
        fill = 0xE62A2A33.toInt(), pressed = 0x40FFFFFF, stroke = 0x30FFFFFF, strokeDp = 1f,
        text = 0xE6FFFFFF.toInt(), accent = 0xFF8E72FF.toInt(),
        dpadFill = 0xF01C1C22.toInt(), aFill = 0xF06A4BD6.toInt(), bFill = 0xF06A4BD6.toInt(),
        faceText = 0xFFFFFFFF.toInt(), pillFill = 0xE63A3A46.toInt(),
    )

    val Neon = ControllerSkin(
        id = "neon", title = R.string.skin_neon,
        fill = 0x40000000, pressed = 0x50FFFFFF, stroke = 0xFFB388FF.toInt(), strokeDp = 2f,
        text = 0xFFF3E8FF.toInt(), accent = 0xC0B388FF.toInt(),
        aFill = 0x40FF4FD8, bFill = 0x4000E5FF, glow = true,
    )

    val Retro = ControllerSkin(
        id = "retro", title = R.string.skin_retro,
        fill = 0xF0D9CFB8.toInt(), pressed = 0x40000000, stroke = 0x40000000, strokeDp = 1f,
        text = 0xFF3A3530.toInt(), accent = 0xF0C8102E.toInt(),
        dpadFill = 0xF0222226.toInt(), aFill = 0xF0C8102E.toInt(), bFill = 0xF0C8102E.toInt(),
        faceText = 0xFFFFFFFF.toInt(), pillText = 0xFF3A3530.toInt(),
    )

    val Pastel = ControllerSkin(
        id = "pastel", title = R.string.skin_pastel,
        fill = 0xE6E0D9FF.toInt(), pressed = 0x33000000, stroke = 0x00000000, strokeDp = 0f,
        text = 0xFF2A2440.toInt(), accent = 0xE6FFD54F.toInt(),
        dpadFill = 0xE6CBBEFF.toInt(), aFill = 0xE6FF9EC4.toInt(), bFill = 0xE69EE6CF.toInt(),
        faceText = 0xFF2A2440.toInt(),
    )

    val all = listOf(Classic, Minimal, Console, Neon, Retro, Pastel)

    fun byId(id: String): ControllerSkin = all.firstOrNull { it.id == id } ?: Classic
}
