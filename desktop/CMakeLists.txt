add_executable(mulator_cli cli.cpp image.cpp)
target_link_libraries(mulator_cli PRIVATE mulator_core)

if(WIN32)
    add_executable(mulator_win WIN32 win32_main.cpp)
    target_link_libraries(mulator_win PRIVATE mulator_core winmm comdlg32 shell32)
endif()

if(MINGW)
    # Ship without needing MinGW runtime DLLs next to the executables.
    target_link_options(mulator_cli PRIVATE -static)
    if(WIN32)
        target_link_options(mulator_win PRIVATE -static)
    endif()
endif()
