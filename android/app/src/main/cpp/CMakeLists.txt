cmake_minimum_required(VERSION 3.22.1)
project(mulator_android CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Only the JNI entry points are exported from libmulator.so; everything else
# (including the core's C++ class names) stays internal to the library.
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

# The platform-independent emulator core lives at the repository root.
set(MULATOR_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../..)
add_subdirectory(${MULATOR_ROOT}/core ${CMAKE_CURRENT_BINARY_DIR}/core)

add_library(mulator SHARED jni_bridge.cpp)
target_link_libraries(mulator PRIVATE mulator_core android log aaudio z)
# Always optimise: an -O0 emulator core is far too slow even for debug builds.
target_compile_options(mulator PRIVATE -O2 -Wall)
target_link_options(mulator PRIVATE -Wl,--exclude-libs,ALL)
