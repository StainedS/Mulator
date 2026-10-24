#pragma once
#include <cstdint>

// Writes RGBA8888 pixels (bytes R,G,B,A) to a PNG file.
bool writePng(const char* path, const uint32_t* rgba, int w, int h);
