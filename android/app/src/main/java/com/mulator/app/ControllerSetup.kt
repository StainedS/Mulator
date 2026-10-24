package com.mulator.app

import com.mulator.app.data.ControllerLayouts
import com.mulator.app.data.ControllerSkins
import com.mulator.app.data.Settings

/** Applies the user's controller preferences (style, size, opacity, saved layouts). */
fun applyControllerSettings(controls: ControllerView) {
    controls.skin = ControllerSkins.byId(Settings.controllerSkin)
    controls.haptics = Settings.haptics
    controls.controlScale = Settings.controllerScale
    controls.showRewind = Settings.rewindEnabled
    controls.alpha = Settings.controllerOpacity
    controls.portraitLayout = ControllerLayouts.load(portrait = true)
    controls.landscapeLayout = ControllerLayouts.load(portrait = false)
}
