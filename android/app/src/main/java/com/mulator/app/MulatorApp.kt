package com.mulator.app

import android.app.Application
import com.mulator.app.data.ControllerLayouts
import com.mulator.app.data.Library
import com.mulator.app.data.Settings

class MulatorApp : Application() {
    override fun onCreate() {
        super.onCreate()
        Settings.init(this)
        Library.init(this)
        ControllerLayouts.init(this)
    }
}
