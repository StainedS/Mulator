// mulator_win — a minimal playable Windows frontend (GDI video, waveOut audio).
//
// Controls: arrows = D-pad, Z = A, X = B, Enter = Start, Backspace = Select,
//           A = L, S = R, Tab (hold) = fast forward, F5 = save state, F7 = load state.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <mmsystem.h>
#include <shellapi.h>

#include <cstdio>
#include <deque>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "mulator/machine.h"

using namespace mulator;

static Machine gMachine;
static HWND gWnd;
static bool gRunning = true;
static uint32_t gBgra[SCREEN_WIDTH * SCREEN_HEIGHT];

static std::vector<uint8_t> readFile(const std::wstring& path) {
    std::vector<uint8_t> data;
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return data;
    uint8_t buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) data.insert(data.end(), buf, buf + n);
    std::fclose(f);
    return data;
}

static void writeFile(const std::wstring& path, const std::vector<uint8_t>& data) {
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return;
    std::fwrite(data.data(), 1, data.size(), f);
    std::fclose(f);
}

// ---------------------------------------------------------------- audio
static constexpr int kRate = 48000;
static constexpr int kBufFrames = 1024;
static constexpr int kNumBufs = 4;
static HWAVEOUT gWave;
static WAVEHDR gHdr[kNumBufs];
static int16_t gBufData[kNumBufs][kBufFrames * 2];
static std::deque<int16_t> gPending;

static void audioInit() {
    WAVEFORMATEX fmt = {};
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 2;
    fmt.nSamplesPerSec = kRate;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = 4;
    fmt.nAvgBytesPerSec = kRate * 4;
    if (waveOutOpen(&gWave, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        gWave = nullptr;
        return;
    }
    for (int i = 0; i < kNumBufs; i++) {
        gHdr[i] = {};
        gHdr[i].lpData = reinterpret_cast<LPSTR>(gBufData[i]);
        gHdr[i].dwBufferLength = sizeof(gBufData[i]);
        gHdr[i].dwFlags = WHDR_DONE;
    }
}

static void audioPump(bool fastForward) {
    int16_t tmp[4096 * 2];
    size_t n;
    while ((n = gMachine.readAudio(tmp, 4096)) > 0)
        if (!fastForward) gPending.insert(gPending.end(), tmp, tmp + n * 2);
    if (gPending.size() > size_t(kRate) * 2 / 4) gPending.clear(); // too far behind: resync
    if (!gWave) { gPending.clear(); return; }
    for (int i = 0; i < kNumBufs; i++) {
        if (!(gHdr[i].dwFlags & WHDR_DONE)) continue;
        if (gPending.size() < size_t(kBufFrames) * 2) break;
        if (gHdr[i].dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(gWave, &gHdr[i], sizeof(WAVEHDR));
        for (int k = 0; k < kBufFrames * 2; k++) {
            gBufData[i][k] = gPending.front();
            gPending.pop_front();
        }
        gHdr[i].dwFlags = 0;
        waveOutPrepareHeader(gWave, &gHdr[i], sizeof(WAVEHDR));
        waveOutWrite(gWave, &gHdr[i], sizeof(WAVEHDR));
    }
}

// ---------------------------------------------------------------- input
static uint16_t keysFromKeyboard() {
    struct { int vk; uint16_t bit; } map[] = {
        {VK_UP, KEY_UP}, {VK_DOWN, KEY_DOWN}, {VK_LEFT, KEY_LEFT}, {VK_RIGHT, KEY_RIGHT},
        {'Z', KEY_A}, {'X', KEY_B}, {VK_RETURN, KEY_START}, {VK_BACK, KEY_SELECT},
        {'A', KEY_L}, {'S', KEY_R},
    };
    uint16_t keys = 0;
    if (GetForegroundWindow() != gWnd) return 0;
    for (auto& m : map)
        if (GetAsyncKeyState(m.vk) & 0x8000) keys |= m.bit;
    return keys;
}

static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_DESTROY:
        gRunning = false;
        PostQuitMessage(0);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = SCREEN_WIDTH;
        bmi.bmiHeader.biHeight = -SCREEN_HEIGHT;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        SetStretchBltMode(dc, COLORONCOLOR);
        StretchDIBits(dc, 0, 0, rc.right, rc.bottom, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, gBgra, &bmi,
                      DIB_RGB_COLORS, SRCCOPY);
        EndPaint(hwnd, &ps);
        return 0;
    }
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

static std::wstring pickRom() {
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = L"Game files\0*.gba\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST;
    return GetOpenFileNameW(&ofn) ? file : L"";
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int show) {
    std::wstring romPath;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc > 1) romPath = argv[1];
    if (argv) LocalFree(argv);
    if (romPath.empty()) romPath = pickRom();
    if (romPath.empty()) return 0;

    auto rom = readFile(romPath);
    if (!gMachine.loadRom(rom.data(), rom.size())) {
        MessageBoxW(nullptr, L"Could not load that ROM.", L"M-ulator", MB_ICONERROR);
        return 1;
    }
    std::wstring base = romPath.substr(0, romPath.find_last_of(L'.'));
    std::wstring savePath = base + L".sav", statePath = base + L".ss0";
    if (gMachine.hasSaveMemory()) {
        auto save = readFile(savePath);
        if (!save.empty()) gMachine.loadSaveData(save.data(), save.size());
    }

    WNDCLASSW wc = {};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"MulatorWindow";
    RegisterClassW(&wc);
    RECT rc = {0, 0, SCREEN_WIDTH * 3, SCREEN_HEIGHT * 3};
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    std::string title = "M-ulator - " + gMachine.romTitle();
    gWnd = CreateWindowA("MulatorWindow", title.c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                         rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, inst, nullptr);
    ShowWindow(gWnd, show);
    audioInit();
    timeBeginPeriod(1);

    LARGE_INTEGER freq, now, next;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&next);
    const double frameTicks = double(freq.QuadPart) / FRAMES_PER_SECOND;
    double acc = double(next.QuadPart);
    int frameCount = 0;
    bool f5 = false, f7 = false;

    while (gRunning) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!gRunning) break;

        bool fast = GetForegroundWindow() == gWnd && (GetAsyncKeyState(VK_TAB) & 0x8000);
        bool f5Now = GetAsyncKeyState(VK_F5) & 0x8000, f7Now = GetAsyncKeyState(VK_F7) & 0x8000;
        if (f5Now && !f5) writeFile(statePath, gMachine.saveState());
        if (f7Now && !f7) {
            auto st = readFile(statePath);
            if (!st.empty()) gMachine.loadState(st.data(), st.size());
        }
        f5 = f5Now;
        f7 = f7Now;

        gMachine.setKeys(keysFromKeyboard());
        int runs = fast ? 4 : 1;
        for (int i = 0; i < runs; i++) gMachine.runFrame();
        audioPump(fast);

        const uint32_t* fb = gMachine.framebuffer();
        for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
            uint32_t p = fb[i];
            gBgra[i] = (p & 0xFF00FF00u) | ((p & 0xFF) << 16) | ((p >> 16) & 0xFF);
        }
        InvalidateRect(gWnd, nullptr, FALSE);
        UpdateWindow(gWnd);

        if (++frameCount % 120 == 0 && gMachine.saveDirty()) {
            writeFile(savePath, gMachine.saveData());
            gMachine.clearSaveDirty();
        }

        if (!fast) {
            acc += frameTicks;
            QueryPerformanceCounter(&now);
            double wait = acc - double(now.QuadPart);
            if (wait > 0) Sleep(DWORD(wait * 1000.0 / double(freq.QuadPart)));
            else if (wait < -frameTicks * 5) acc = double(now.QuadPart);
        } else {
            QueryPerformanceCounter(&now);
            acc = double(now.QuadPart);
        }
    }

    if (gMachine.hasSaveMemory() && gMachine.saveDirty()) writeFile(savePath, gMachine.saveData());
    if (gWave) {
        waveOutReset(gWave);
        waveOutClose(gWave);
    }
    timeEndPeriod(1);
    return 0;
}
