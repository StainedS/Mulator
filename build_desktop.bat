@echo off
rem Builds the desktop tools (mulator_cli.exe, mulator_win.exe) with MinGW g++,
rem using the CMake + Ninja that ship with the Android SDK.
setlocal
set ROOT=%~dp0
set PATH=%LOCALAPPDATA%\Android\Sdk\cmake\3.22.1\bin;C:\MinGW\bin;%PATH%
cmake -S "%ROOT%." -B "%ROOT%build\desktop" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++ || exit /b 1
cmake --build "%ROOT%build\desktop" || exit /b 1
echo.
echo Built: %ROOT%build\desktop\desktop\mulator_cli.exe and mulator_win.exe
