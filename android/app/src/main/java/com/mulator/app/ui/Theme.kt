package com.mulator.app.ui

import android.os.Build
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Shapes
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.dynamicDarkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.mulator.app.data.Settings

/** Brand colours, taken from the launcher icon (violet with a gold accent). */
object Brand {
    val Violet = Color(0xFF7C5CFF)
    val Gold = Color(0xFFFFD54F)
}

private val MulatorDark = darkColorScheme(
    primary = Color(0xFFA58FFF),
    onPrimary = Color(0xFF1D0B66),
    primaryContainer = Color(0xFF4A32B8),
    onPrimaryContainer = Color(0xFFE6DEFF),
    secondary = Brand.Gold,
    onSecondary = Color(0xFF3B2F00),
    secondaryContainer = Color(0xFF3A3352),
    onSecondaryContainer = Color(0xFFE8E0FF),
    tertiary = Color(0xFF6FDBC4),
    background = Color(0xFF0D0D13),
    onBackground = Color(0xFFECECF4),
    surface = Color(0xFF0D0D13),
    onSurface = Color(0xFFECECF4),
    surfaceVariant = Color(0xFF262633),
    onSurfaceVariant = Color(0xFFA6A6BC),
    surfaceContainerLowest = Color(0xFF08080D),
    surfaceContainerLow = Color(0xFF13131B),
    surfaceContainer = Color(0xFF181822),
    surfaceContainerHigh = Color(0xFF20202C),
    surfaceContainerHighest = Color(0xFF2A2A38),
    outline = Color(0xFF4A4A5E),
    outlineVariant = Color(0xFF2E2E3C),
    scrim = Color(0xFF000000),
)

private val MulatorType = Typography().run {
    copy(
        displaySmall = displaySmall.copy(fontWeight = FontWeight.Black, letterSpacing = (-0.5).sp),
        headlineLarge = headlineLarge.copy(fontWeight = FontWeight.Black, letterSpacing = (-0.5).sp),
        headlineSmall = headlineSmall.copy(fontWeight = FontWeight.Bold),
        titleLarge = titleLarge.copy(fontWeight = FontWeight.Bold),
        titleMedium = titleMedium.copy(fontWeight = FontWeight.SemiBold),
        labelLarge = labelLarge.copy(fontWeight = FontWeight.SemiBold),
    )
}

private val MulatorShapes = Shapes(
    small = RoundedCornerShape(10.dp),
    medium = RoundedCornerShape(18.dp),
    large = RoundedCornerShape(26.dp),
    extraLarge = RoundedCornerShape(32.dp),
)

@Composable
fun MulatorTheme(content: @Composable () -> Unit) {
    val colors = if (Settings.dynamicColor && Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
        dynamicDarkColorScheme(LocalContext.current)
    } else {
        MulatorDark
    }
    MaterialTheme(colorScheme = colors, typography = MulatorType, shapes = MulatorShapes, content = content)
}
