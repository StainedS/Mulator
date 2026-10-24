// Frame scheduler and the public Machine facade.
#include <algorithm>

#include "mulator/machine.h"
#include "system.h"

namespace mulator {

// Minimal replacement BIOS used when no real BIOS is supplied. SWIs are
// handled in C++ (bios_hle.cpp); this ARM code only provides the exception
// vectors and the IRQ dispatcher that games expect at 0x18 / 0x128.
static void installHleBios(u8* bios) {
    std::memset(bios, 0, 0x4000);
    auto put = [&](u32 addr, u32 op) { wr32(bios + addr, op); };
    put(0x00, 0xE3A0F408); // mov pc, #0x08000000      (reset)
    put(0x08, 0xE1B0F00E); // movs pc, lr               (swi, unused in HLE)
    put(0x18, 0xEA000042); // b 0x128                   (irq)
    put(0x1C, 0xE25EF004); // subs pc, lr, #4           (fiq)
    put(0x128, 0xE92D500F); // stmfd sp!, {r0-r3, r12, lr}
    put(0x12C, 0xE3A00301); // mov r0, #0x04000000
    put(0x130, 0xE28FE000); // add lr, pc, #0
    put(0x134, 0xE510F004); // ldr pc, [r0, #-4]
    put(0x138, 0xE8BD500F); // ldmfd sp!, {r0-r3, r12, lr}
    put(0x13C, 0xE25EF004); // subs pc, lr, #4
    // Never executed; prefetched after returning from an IRQ, which is the
    // value games then see when reading the read-protected BIOS.
    put(0x144, 0xE55EC002);
}

System::System() : cpu(*this), bus(*this), ppu(*this), apu(*this), timers(*this), dma(*this) {}

void System::reset() {
    if (!bus.hasBios) installHleBios(bus.bios);
    bus.reset();
    ppu.reset();
    apu.reset();
    timers.reset();
    dma.reset();
    cart.resetState();
    cpu.reset(!bus.hasBios);
    cycleDebt = 0;
}

void System::requestIrq(u16 bits) {
    bus.ioSet16(reg::IF, bus.ioRead16(reg::IF) | bits);
}

void System::checkKeypadIrq() {
    u16 cnt = bus.ioRead16(reg::KEYCNT);
    if (!(cnt & 0x4000)) return;
    u16 mask = cnt & 0x3FF;
    bool fire = (cnt & 0x8000) ? (keys & mask) == mask && mask : (keys & mask) != 0;
    if (fire) requestIrq(IRQ_KEYPAD);
}

void System::runCycles(int n) {
    cycleDebt += n;
    while (cycleDebt > 0) {
        u16 pending = bus.ioRead16(reg::IE) & bus.ioRead16(reg::IF);
        if (cpu.halted) {
            if (pending) {
                cpu.halted = false;
            } else {
                int step = cycleDebt;
                if (timers.anyRunning()) step = std::min(step, timers.cyclesUntilOverflow());
                if (step < 1) step = 1;
                timers.tick(step);
                apu.tick(step);
                cycleDebt -= step;
                continue;
            }
        }
        if (pending && (bus.io[reg::IME] & 1) && cpu.irqEnabled()) cpu.raiseIrq();
        int c = cpu.step();
        timers.tick(c);
        apu.tick(c);
        cycleDebt -= c;
    }
}

void System::startLine(int line) {
    bus.ioSet16(reg::VCOUNT, u16(line));
    u16 stat = bus.ioRead16(reg::DISPSTAT) & ~2u;
    if (line == VISIBLE_LINES) {
        stat |= 1;
        if (stat & 0x08) requestIrq(IRQ_VBLANK);
    } else if (line == TOTAL_LINES - 1) {
        stat &= ~1u;
    }
    if (line == (stat >> 8)) {
        stat |= 4;
        if (stat & 0x20) requestIrq(IRQ_VCOUNT);
    } else {
        stat &= ~4u;
    }
    bus.ioSet16(reg::DISPSTAT, stat);
    if (line == VISIBLE_LINES) {
        ppu.vblankLatch();
        dma.trigger(1);
        checkKeypadIrq();
    }
}

void System::hblank(int line) {
    u16 stat = bus.ioRead16(reg::DISPSTAT) | 2;
    bus.ioSet16(reg::DISPSTAT, stat);
    if (stat & 0x10) requestIrq(IRQ_HBLANK);
    if (line < VISIBLE_LINES) {
        ppu.renderLine(line);
        dma.trigger(2);
        ppu.lineDone();
    }
}

void System::runFrame() {
    for (int line = 0; line < TOTAL_LINES; line++) {
        startLine(line);
        runCycles(CYCLES_HDRAW);
        hblank(line);
        runCycles(CYCLES_HBLANK);
    }
}

// ---------------------------------------------------------------- public facade
Machine::Machine() : sys(new System) {
    sys->apu.setSampleRate(48000);
}

Machine::~Machine() = default;

bool Machine::loadBios(const uint8_t* data, size_t size) {
    if (size != 0x4000) return false;
    std::memcpy(sys->bus.bios, data, size);
    sys->bus.hasBios = true;
    return true;
}

bool Machine::loadRom(const uint8_t* data, size_t size) {
    if (size < 0xC0 || size > 0x2000000) return false;
    sys->cart.load(data, size);
    reset();
    return true;
}

bool Machine::romLoaded() const { return !sys->cart.rom.empty(); }

void Machine::reset() { sys->reset(); }

void Machine::runFrame() {
    if (romLoaded()) sys->runFrame();
}

const uint32_t* Machine::framebuffer() const { return sys->ppu.framebuffer; }

void Machine::setKeys(uint16_t pressed) {
    sys->keys = pressed & 0x3FF;
    sys->checkKeypadIrq();
}

void Machine::setAudioSampleRate(int hz) { sys->apu.setSampleRate(hz); }

size_t Machine::readAudio(int16_t* out, size_t maxFrames) { return sys->apu.read(out, maxFrames); }

bool Machine::hasSaveMemory() const { return !sys->cart.save.empty(); }

std::vector<uint8_t> Machine::saveData() const { return sys->cart.save; }

bool Machine::loadSaveData(const uint8_t* data, size_t size) {
    auto& save = sys->cart.save;
    if (save.empty() || size == 0) return false;
    // EEPROM saves may be 512 bytes; Flash 64K saves may be loaded into a 128K chip.
    std::fill(save.begin(), save.end(), 0xFF);
    std::memcpy(save.data(), data, std::min(size, save.size()));
    if (sys->cart.saveType == SaveType::Eeprom && size == 512) sys->cart.eeprom.addrBits = 6;
    return true;
}

bool Machine::saveDirty() const { return sys->cart.dirty; }
void Machine::clearSaveDirty() { sys->cart.dirty = false; }

static const char kStateMagic[8] = {'M', '-', 'U', 'L', 'S', 'T', '0', '2'};

std::vector<uint8_t> Machine::saveState() {
    StateWriter w;
    char magic[8];
    std::memcpy(magic, kStateMagic, 8);
    w.pod(magic);
    char code[4] = {};
    std::string gc = gameCode();
    std::memcpy(code, gc.data(), std::min<size_t>(gc.size(), 4));
    w.pod(code);
    sys->serialize(w);
    return std::move(w.buf);
}

bool Machine::loadState(const uint8_t* data, size_t size) {
    if (!romLoaded() || size < 12 || std::memcmp(data, kStateMagic, 8) != 0) return false;
    std::string gc = gameCode();
    char code[4] = {};
    std::memcpy(code, gc.data(), std::min<size_t>(gc.size(), 4));
    if (std::memcmp(data + 8, code, 4) != 0) return false;

    std::vector<uint8_t> backup = saveState();
    StateReader r(data + 12, size - 12);
    sys->serialize(r);
    if (!r.ok || r.pos != r.size) {
        StateReader restore(backup.data() + 12, backup.size() - 12);
        sys->serialize(restore);
        sys->bus.updateWaitStates();
        return false;
    }
    sys->bus.updateWaitStates();
    sys->cart.dirty = true;
    return true;
}

std::string Machine::romTitle() const { return sys->cart.title(); }
std::string Machine::gameCode() const { return sys->cart.gameCode(); }

std::string Machine::saveTypeName() const {
    switch (sys->cart.saveType) {
    case SaveType::Sram: return "SRAM";
    case SaveType::Flash64: return "Flash 64K";
    case SaveType::Flash128: return "Flash 128K";
    case SaveType::Eeprom: return "EEPROM";
    default: return "None";
    }
}

} // namespace mulator
