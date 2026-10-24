// mulator_cli — headless runner used to test the core on a PC.
//
//   mulator_cli <game file> [--frames N] [--png out.png] [--shot FRAME:file.png]...
//                        [--press KEYS@FIRST-LAST]... [--bios bios.bin]
//                        [--wav out.wav] [--state-test]
//
// KEYS is a '+'-separated list of A B SELECT START RIGHT LEFT UP DOWN R L.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "image.h"
#include "mulator/machine.h"

using namespace mulator;

static std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

static uint16_t parseKeys(const std::string& s) {
    static const struct { const char* name; uint16_t bit; } names[] = {
        {"A", KEY_A}, {"B", KEY_B}, {"SELECT", KEY_SELECT}, {"START", KEY_START},
        {"RIGHT", KEY_RIGHT}, {"LEFT", KEY_LEFT}, {"UP", KEY_UP}, {"DOWN", KEY_DOWN},
        {"R", KEY_R}, {"L", KEY_L},
    };
    uint16_t mask = 0;
    size_t pos = 0;
    while (pos <= s.size()) {
        size_t end = s.find('+', pos);
        if (end == std::string::npos) end = s.size();
        std::string tok = s.substr(pos, end - pos);
        for (auto& n : names)
            if (tok == n.name) mask |= n.bit;
        pos = end + 1;
    }
    return mask;
}

struct Press { uint16_t keys; int first, last; };
struct Shot { int frame; std::string path; };

static void writeWav(const std::string& path, const std::vector<int16_t>& samples, int rate) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    uint32_t dataBytes = uint32_t(samples.size() * 2);
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); u32(36 + dataBytes); std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(1); u16(2); u32(uint32_t(rate)); u32(uint32_t(rate * 4)); u16(4); u16(16);
    std::fwrite("data", 1, 4, f); u32(dataBytes);
    std::fwrite(samples.data(), 2, samples.size(), f);
    std::fclose(f);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <game file> [--frames N] [--png out.png] [--shot F:file.png] "
                             "[--press KEYS@F1-F2] [--bios file] [--wav out.wav] [--state-test]\n", argv[0]);
        return 2;
    }
    std::string romPath = argv[1], pngPath, biosPath, wavPath;
    int frames = 600;
    bool stateTest = false;
    std::vector<Press> presses;
    std::vector<Shot> shots;
    for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--frames") frames = std::atoi(next().c_str());
        else if (a == "--png") pngPath = next();
        else if (a == "--bios") biosPath = next();
        else if (a == "--wav") wavPath = next();
        else if (a == "--state-test") stateTest = true;
        else if (a == "--shot") {
            std::string v = next();
            size_t c = v.find(':');
            if (c != std::string::npos) shots.push_back({std::atoi(v.substr(0, c).c_str()), v.substr(c + 1)});
        } else if (a == "--press") {
            std::string v = next();
            size_t at = v.find('@'), dash = v.find('-', at);
            if (at == std::string::npos) continue;
            Press p;
            p.keys = parseKeys(v.substr(0, at));
            p.first = std::atoi(v.substr(at + 1).c_str());
            p.last = dash == std::string::npos ? p.first : std::atoi(v.substr(dash + 1).c_str());
            presses.push_back(p);
        }
    }

    Machine machine;
    if (!biosPath.empty()) {
        auto bios = readFile(biosPath);
        if (!machine.loadBios(bios.data(), bios.size())) std::fprintf(stderr, "warning: bad BIOS file\n");
    }
    auto rom = readFile(romPath);
    if (!machine.loadRom(rom.data(), rom.size())) {
        std::fprintf(stderr, "error: could not load ROM '%s'\n", romPath.c_str());
        return 1;
    }
    std::printf("ROM: \"%s\" [%s], %zu bytes, save: %s\n", machine.romTitle().c_str(), machine.gameCode().c_str(),
                rom.size(), machine.saveTypeName().c_str());

    std::vector<int16_t> audio, chunk(4096 * 2);
    int peak = 0;
    auto start = std::chrono::steady_clock::now();
    std::vector<uint8_t> midState;
    for (int f = 0; f < frames; f++) {
        uint16_t keys = 0;
        for (auto& p : presses)
            if (f >= p.first && f <= p.last) keys |= p.keys;
        machine.setKeys(keys);
        machine.runFrame();
        size_t n;
        while ((n = machine.readAudio(chunk.data(), 4096)) > 0) {
            for (size_t i = 0; i < n * 2; i++) peak = std::max(peak, std::abs(int(chunk[i])));
            if (!wavPath.empty()) audio.insert(audio.end(), chunk.begin(), chunk.begin() + n * 2);
        }
        for (auto& s : shots)
            if (s.frame == f + 1) writePng(s.path.c_str(), machine.framebuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
        if (stateTest && f == frames / 2) midState = machine.saveState();
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("Ran %d frames in %.2fs (%.0f fps, %.0f%% of real time); audio peak %d\n", frames, secs,
                frames / secs, frames / secs / FRAMES_PER_SECOND * 100.0, peak);

    if (!pngPath.empty()) {
        if (writePng(pngPath.c_str(), machine.framebuffer(), SCREEN_WIDTH, SCREEN_HEIGHT))
            std::printf("Wrote %s\n", pngPath.c_str());
    }
    if (!wavPath.empty()) writeWav(wavPath, audio, 48000);

    if (stateTest && !midState.empty()) {
        // Re-run the second half from the saved state; the final frame must match.
        std::vector<uint32_t> expected(machine.framebuffer(), machine.framebuffer() + SCREEN_WIDTH * SCREEN_HEIGHT);
        if (!machine.loadState(midState.data(), midState.size())) {
            std::printf("State test: FAILED to load state\n");
            return 1;
        }
        for (int f = frames / 2 + 1; f < frames; f++) {
            uint16_t keys = 0;
            for (auto& p : presses)
                if (f >= p.first && f <= p.last) keys |= p.keys;
            machine.setKeys(keys);
            machine.runFrame();
        }
        bool same = std::memcmp(expected.data(), machine.framebuffer(), expected.size() * 4) == 0;
        std::printf("State test (%zu bytes): %s\n", midState.size(), same ? "OK, replay matches" : "MISMATCH");
        if (!same) return 1;
    }
    return 0;
}
