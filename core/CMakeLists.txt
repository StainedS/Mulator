cmake_minimum_required(VERSION 3.18)

add_library(mulator_core STATIC
    src/apu.cpp
    src/bios_hle.cpp
    src/bus.cpp
    src/cart.cpp
    src/cpu.cpp
    src/ppu.cpp
    src/system.cpp
    src/timers_dma.cpp
)
target_include_directories(mulator_core PUBLIC include PRIVATE src)
target_compile_features(mulator_core PUBLIC cxx_std_17)

if(MSVC)
    target_compile_options(mulator_core PRIVATE /W3 /O2)
else()
    target_compile_options(mulator_core PRIVATE -Wall -O2)
endif()
