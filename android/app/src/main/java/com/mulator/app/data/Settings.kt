package com.mulator.app.data

import android.content.Context
import android.content.SharedPreferences
import androidx.compose.runtime.MutableState
import androidx.compose.runtime.mutableStateOf
import kotlin.properties.ReadWriteProperty
import kotlin.reflect.KProperty

/** User preferences, persisted in SharedPreferences and observable from Compose. */
object Settings {
    private lateinit var prefs: SharedPreferences

    fun init(context: Context) {
        if (::prefs.isInitialized) return
        prefs = context.applicationContext.getSharedPreferences("settings", Context.MODE_PRIVATE)
    }

    /** Reopen a game exactly where it was left, using an automatic save state. */
    var resumeOnLaunch by bool("resume_on_launch", true)
    var rewindEnabled by bool("rewind", true)
    /** Fast-forward multiplier; 0 means as fast as the device can go. */
    var fastForwardSpeed by int("ff_speed", 3)
    var haptics by bool("haptics", true)
    var controllerOpacity by float("pad_opacity", 0.8f)
    var controllerScale by float("pad_scale", 1f)
    /** Id of a [ControllerSkin] in [ControllerSkins]. */
    var controllerSkin by string("pad_skin", "classic")
    var dynamicColor by bool("dynamic_color", false)

    private fun bool(key: String, default: Boolean) =
        Pref({ getBoolean(key, default) }, { putBoolean(key, it) })

    private fun int(key: String, default: Int) =
        Pref({ getInt(key, default) }, { putInt(key, it) })

    private fun string(key: String, default: String) =
        Pref({ getString(key, default) ?: default }, { putString(key, it) })

    private fun float(key: String, default: Float) =
        Pref({ getFloat(key, default) }, { putFloat(key, it) })

    private class Pref<T>(
        private val read: SharedPreferences.() -> T,
        private val write: SharedPreferences.Editor.(T) -> Unit,
    ) : ReadWriteProperty<Settings, T> {
        private val state: MutableState<T> by lazy { mutableStateOf(prefs.read()) }

        override fun getValue(thisRef: Settings, property: KProperty<*>): T = state.value

        override fun setValue(thisRef: Settings, property: KProperty<*>, value: T) {
            state.value = value
            prefs.edit().apply { write(value) }.apply()
        }
    }
}
