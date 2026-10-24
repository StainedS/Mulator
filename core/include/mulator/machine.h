// M-ulator — public API of the emulation core.
// The core is platform independent: frontends (desktop, Android) feed it a ROM,
// call runFrame() ~59.73 times per second, and pull video/audio out of it.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mulator {

enum Key : uint16_t {
    KEY_A = 1 << 0,
    KEY_B = 1 << 1,
    KEY_SELECT = 1 << 2,
    KEY_START = 1 << 3,
    KEY_RIGHT = 1 << 4,
    KEY_LEFT = 1 << 5,
    KEY_UP = 1 << 6,
    KEY_DOWN = 1 << 7,
    KEY_R = 1 << 8,
    KEY_L = 1 << 9,
};

constexpr int SCREEN_WIDTH = 240;
constexpr int SCREEN_HEIGHT = 160;
constexpr double FRAMES_PER_SECOND = 16777216.0 / (1232.0 * 228.0); // ~59.7275

class System;

class Machine {
public:
    Machine();
    ~Machine();
    Machine(const Machine&) = delete;
    Machine& operator=(const Machine&) = delete;

    // Optional: a real BIOS dump. Without it a built-in high-level BIOS is used.
    bool loadBios(const uint8_t* data, size_t size);
    bool loadRom(const uint8_t* data, size_t size);
    bool romLoaded() const;
    void reset();

    // Emulates one full video frame (280896 CPU cycles).
    void runFrame();

    // 240x160 pixels, each uint32 is 0xAABBGGRR (bytes R,G,B,A in memory).
    const uint32_t* framebuffer() const;

    // Bitmask of mulator::Key values currently held down.
    void setKeys(uint16_t pressed);

    // Audio: interleaved stereo int16. Samples accumulate while frames run.
    void setAudioSampleRate(int hz);
    size_t readAudio(int16_t* out, size_t maxFrames);

    // Battery save (SRAM / Flash / EEPROM contents).
    bool hasSaveMemory() const;
    std::vector<uint8_t> saveData() const;
    bool loadSaveData(const uint8_t* data, size_t size);
    bool saveDirty() const;
    void clearSaveDirty();

    // Save states.
    std::vector<uint8_t> saveState();
    bool loadState(const uint8_t* data, size_t size);

    std::string romTitle() const;
    std::string gameCode() const;
    std::string saveTypeName() const;

private:
    std::unique_ptr<System> sys;
};

} // namespace mulator
