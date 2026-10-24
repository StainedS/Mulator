// Memory map, I/O register dispatch and wait-state timing.
#include "system.h"

namespace mulator {

void Bus::reset() {
    std::memset(ewram, 0, sizeof(ewram));
    std::memset(iwram, 0, sizeof(iwram));
    std::memset(io, 0, sizeof(io));
    std::memset(pal, 0, sizeof(pal));
    std::memset(vram, 0, sizeof(vram));
    std::memset(oam, 0, sizeof(oam));
    cycles = 0;
    openBus = 0;
    biosLatch = 0xE129F000;
    ioSet16(reg::KEYINPUT, 0x3FF);
    ioSet16(reg::SOUNDBIAS, 0x200);
    ioSet16(reg::BG2PA, 0x100);
    ioSet16(reg::BG2PA + 6, 0x100);
    ioSet16(reg::BG3PA, 0x100);
    ioSet16(reg::BG3PA + 6, 0x100);
    if (!hasBios) io[reg::POSTFLG] = 1;
    updateWaitStates();
}

void Bus::updateWaitStates() {
    static const u8 nTable[4] = {4, 3, 2, 8};
    u16 w = ioRead16(reg::WAITCNT);
    const u8 base16[8] = {1, 1, 3, 1, 1, 1, 1, 1};
    const u8 base32[8] = {1, 1, 6, 1, 1, 2, 2, 1};
    for (int i = 0; i < 8; i++) {
        waitN16[i] = waitS16[i] = base16[i];
        waitN32[i] = waitS32[i] = base32[i];
    }
    u8 n[3] = {u8(1 + nTable[(w >> 2) & 3]), u8(1 + nTable[(w >> 5) & 3]), u8(1 + nTable[(w >> 8) & 3])};
    u8 s[3] = {u8(1 + (((w >> 4) & 1) ? 1 : 2)), u8(1 + (((w >> 7) & 1) ? 1 : 4)),
               u8(1 + (((w >> 10) & 1) ? 1 : 8))};
    for (int ws = 0; ws < 3; ws++) {
        for (int k = 0; k < 2; k++) {
            int region = 8 + ws * 2 + k;
            waitN16[region] = n[ws];
            waitS16[region] = s[ws];
            waitN32[region] = u8(n[ws] + s[ws]);
            waitS32[region] = u8(2 * s[ws]);
        }
    }
    u8 sram = u8(1 + nTable[w & 3]);
    for (int region = 0xE; region <= 0xF; region++) {
        waitN16[region] = waitS16[region] = waitN32[region] = waitS32[region] = sram;
    }
}

void Bus::addWait(u32 addr, bool seq, bool wide, bool code) {
    u32 region = addr >> 24;
    if (region > 0xF) { cycles += 1; return; }
    if (code && seq && region >= 0x8 && region <= 0xD && (ioRead16(reg::WAITCNT) & 0x4000)) {
        cycles += wide ? 2 : 1; // game pak prefetch buffer hit
        return;
    }
    if (wide) cycles += seq ? waitS32[region] : waitN32[region];
    else cycles += seq ? waitS16[region] : waitN16[region];
}

static inline u32 vramOffset(u32 addr) {
    u32 off = addr & 0x1FFFF;
    return off >= 0x18000 ? off - 0x8000 : off;
}

// ---------------------------------------------------------------- raw reads
u32 Bus::romRead16(u32 addr) {
    u32 off = addr & 0x01FFFFFE;
    if (sys.cart.isEepromAddr(addr)) return sys.cart.eepromRead();
    if (off >= 0xC4 && off <= 0xC8) {
        u16 v;
        if (sys.cart.gpioRead(addr, v)) return v;
    }
    if (off + 1 < sys.cart.rom.size()) return rd16(&sys.cart.rom[off]);
    return (addr >> 1) & 0xFFFF; // open bus on the cartridge returns the address
}

static inline bool isBackup(u32 addr) { return (addr >> 24) == 0xE || (addr >> 24) == 0xF; }

u32 Bus::readRaw32(u32 addr) {
    if (isBackup(addr)) return sys.cart.readBackup(addr) * 0x01010101u;
    addr &= ~3u;
    switch (addr >> 24) {
    case 0x0:
        if (addr < 0x4000) {
            if (sys.cpu.curAddr < 0x4000) return rd32(bios + addr);
            return biosLatch;
        }
        return openBus;
    case 0x2: return rd32(ewram + (addr & 0x3FFFF));
    case 0x3: return rd32(iwram + (addr & 0x7FFF));
    case 0x4: {
        u32 off = addr & 0xFFFFFF;
        if (off >= 0x400) return openBus;
        return u32(ioRead8(off)) | (u32(ioRead8(off + 1)) << 8) | (u32(ioRead8(off + 2)) << 16) |
               (u32(ioRead8(off + 3)) << 24);
    }
    case 0x5: return rd32(pal + (addr & 0x3FF));
    case 0x6: return rd32(vram + vramOffset(addr));
    case 0x7: return rd32(oam + (addr & 0x3FF));
    case 0x8: case 0x9: case 0xA: case 0xB: case 0xC: case 0xD:
        return romRead16(addr) | (romRead16(addr + 2) << 16);
    default: return openBus;
    }
}

u16 Bus::readRaw16(u32 addr) {
    if (isBackup(addr)) return u16(sys.cart.readBackup(addr) * 0x0101u);
    addr &= ~1u;
    switch (addr >> 24) {
    case 0x0:
        if (addr < 0x4000) {
            if (sys.cpu.curAddr < 0x4000) return rd16(bios + addr);
            return u16(biosLatch >> ((addr & 2) * 8));
        }
        return u16(openBus >> ((addr & 2) * 8));
    case 0x2: return rd16(ewram + (addr & 0x3FFFF));
    case 0x3: return rd16(iwram + (addr & 0x7FFF));
    case 0x4: {
        u32 off = addr & 0xFFFFFF;
        if (off >= 0x400) return u16(openBus >> ((addr & 2) * 8));
        return u16(ioRead8(off) | (ioRead8(off + 1) << 8));
    }
    case 0x5: return rd16(pal + (addr & 0x3FF));
    case 0x6: return rd16(vram + vramOffset(addr));
    case 0x7: return rd16(oam + (addr & 0x3FF));
    case 0x8: case 0x9: case 0xA: case 0xB: case 0xC: case 0xD:
        return u16(romRead16(addr));
    default: return u16(openBus >> ((addr & 2) * 8));
    }
}

u8 Bus::readRaw8(u32 addr) {
    switch (addr >> 24) {
    case 0x2: return ewram[addr & 0x3FFFF];
    case 0x3: return iwram[addr & 0x7FFF];
    case 0x4: {
        u32 off = addr & 0xFFFFFF;
        if (off >= 0x400) return u8(openBus >> ((addr & 3) * 8));
        return ioRead8(off);
    }
    case 0xE: case 0xF: return sys.cart.readBackup(addr);
    default: return u8(readRaw16(addr) >> ((addr & 1) * 8));
    }
}

// ---------------------------------------------------------------- raw writes
void Bus::writeRaw8(u32 addr, u8 v) {
    switch (addr >> 24) {
    case 0x2: ewram[addr & 0x3FFFF] = v; return;
    case 0x3: iwram[addr & 0x7FFF] = v; return;
    case 0x4: {
        u32 off = addr & 0xFFFFFF;
        if (off < 0x400) ioWrite8(off, v);
        return;
    }
    case 0x5: wr16(pal + (addr & 0x3FE), u16(v * 0x0101)); return;
    case 0x6: {
        u32 off = vramOffset(addr) & ~1u;
        bool bitmap = (ioRead16(reg::DISPCNT) & 7) >= 3;
        if (off < (bitmap ? 0x14000u : 0x10000u)) wr16(vram + off, u16(v * 0x0101));
        return;
    }
    case 0x7: return; // byte writes to OAM are ignored
    case 0xE: case 0xF: sys.cart.writeBackup(addr, v); return;
    default: return;
    }
}

void Bus::writeRaw16(u32 addr, u16 v) {
    if (isBackup(addr)) { sys.cart.writeBackup(addr, u8(v >> ((addr & 1) * 8))); return; }
    addr &= ~1u;
    switch (addr >> 24) {
    case 0x2: wr16(ewram + (addr & 0x3FFFF), v); return;
    case 0x3: wr16(iwram + (addr & 0x7FFF), v); return;
    case 0x4: {
        u32 off = addr & 0xFFFFFF;
        if (off < 0x400) { ioWrite8(off, u8(v)); ioWrite8(off + 1, u8(v >> 8)); }
        return;
    }
    case 0x5: wr16(pal + (addr & 0x3FF), v); return;
    case 0x6: wr16(vram + vramOffset(addr), v); return;
    case 0x7: wr16(oam + (addr & 0x3FF), v); return;
    case 0x8: case 0x9: case 0xA: case 0xB: case 0xC: case 0xD:
        if (sys.cart.isEepromAddr(addr)) sys.cart.eepromWrite(v);
        else if ((addr & 0x01FFFFFF) - 0xC4 <= 4) sys.cart.gpioWrite(addr, v);
        return;
    default: return;
    }
}

void Bus::writeRaw32(u32 addr, u32 v) {
    if (isBackup(addr)) { sys.cart.writeBackup(addr, u8(v >> ((addr & 3) * 8))); return; }
    addr &= ~3u;
    switch (addr >> 24) {
    case 0x2: wr32(ewram + (addr & 0x3FFFF), v); return;
    case 0x3: wr32(iwram + (addr & 0x7FFF), v); return;
    case 0x4: {
        u32 off = addr & 0xFFFFFF;
        if (off < 0x400) {
            for (int i = 0; i < 4; i++) ioWrite8(off + i, u8(v >> (i * 8)));
        }
        return;
    }
    case 0x5: wr32(pal + (addr & 0x3FF), v); return;
    case 0x6: wr32(vram + vramOffset(addr), v); return;
    case 0x7: wr32(oam + (addr & 0x3FF), v); return;
    default: writeRaw16(addr, u16(v)); writeRaw16(addr + 2, u16(v >> 16)); return;
    }
}

// ---------------------------------------------------------------- timed accessors
u32 Bus::read32(u32 addr, bool seq) { addWait(addr, seq, true, false); return readRaw32(addr); }
u16 Bus::read16(u32 addr, bool seq) { addWait(addr, seq, false, false); return readRaw16(addr); }
u8 Bus::read8(u32 addr, bool seq) { addWait(addr, seq, false, false); return readRaw8(addr); }
void Bus::write32(u32 addr, u32 v, bool seq) { addWait(addr, seq, true, false); writeRaw32(addr, v); }
void Bus::write16(u32 addr, u16 v, bool seq) { addWait(addr, seq, false, false); writeRaw16(addr, v); }
void Bus::write8(u32 addr, u8 v, bool seq) { addWait(addr, seq, false, false); writeRaw8(addr, v); }

u32 Bus::fetch32(u32 addr, bool seq) {
    addWait(addr, seq, true, true);
    u32 v;
    if (addr < 0x4000) {
        v = rd32(bios + (addr & ~3u));
        biosLatch = v;
    } else {
        v = readRaw32(addr);
    }
    openBus = v;
    return v;
}

u16 Bus::fetch16(u32 addr, bool seq) {
    addWait(addr, seq, false, true);
    u16 v;
    if (addr < 0x4000) {
        v = rd16(bios + (addr & ~1u));
        biosLatch = rd32(bios + (addr & ~3u));
    } else {
        v = readRaw16(addr);
    }
    openBus = v * 0x00010001u;
    return v;
}

// ---------------------------------------------------------------- I/O registers
u8 Bus::ioRead8(u32 off) {
    u32 half = off & ~1u;
    u16 v;
    switch (half) {
    // write-only registers
    case 0x010: case 0x012: case 0x014: case 0x016: case 0x018: case 0x01A: case 0x01C: case 0x01E:
    case 0x020: case 0x022: case 0x024: case 0x026: case 0x028: case 0x02A: case 0x02C: case 0x02E:
    case 0x030: case 0x032: case 0x034: case 0x036: case 0x038: case 0x03A: case 0x03C: case 0x03E:
    case 0x040: case 0x042: case 0x044: case 0x046: case 0x04C: case 0x04E: case 0x054: case 0x056:
    case 0x0A0: case 0x0A2: case 0x0A4: case 0x0A6:
    case 0x0B0: case 0x0B2: case 0x0B4: case 0x0B6: case 0x0B8:
    case 0x0BC: case 0x0BE: case 0x0C0: case 0x0C2: case 0x0C4:
    case 0x0C8: case 0x0CA: case 0x0CC: case 0x0CE: case 0x0D0:
    case 0x0D4: case 0x0D6: case 0x0D8: case 0x0DA: case 0x0DC:
        v = 0;
        break;
    case 0x062: case 0x068: v = ioRead16(half) & 0xFFC0; break;
    case 0x064: case 0x06C: case 0x074: v = ioRead16(half) & 0x4000; break;
    case 0x072: v = ioRead16(half) & 0xE000; break;
    case 0x078: v = ioRead16(half) & 0xFF00; break;
    case 0x07C: v = ioRead16(half) & 0x40FF; break;
    case 0x084: {
        v = ioRead16(half) & 0x0080;
        if (sys.apu.ch[0].on) v |= 1;
        if (sys.apu.ch[1].on) v |= 2;
        if (sys.apu.wave.on) v |= 4;
        if (sys.apu.noise.on) v |= 8;
        break;
    }
    case 0x090: case 0x092: case 0x094: case 0x096: case 0x098: case 0x09A: case 0x09C: case 0x09E: {
        int bank = (io[0x70] & 0x40) ? 0 : 1;
        return sys.apu.wave.ram[bank * 16 + (off - 0x90)];
    }
    case 0x100: case 0x104: case 0x108: case 0x10C:
        v = sys.timers.readCounter((half - 0x100) / 4);
        break;
    case reg::KEYINPUT:
        v = u16(~sys.keys & 0x3FF);
        break;
    default:
        v = ioRead16(half);
        break;
    }
    return u8(v >> ((off & 1) * 8));
}

void Bus::ioWrite8(u32 off, u8 v) {
    switch (off) {
    case reg::IF: case reg::IF + 1:
        io[off] &= u8(~v);
        return;
    case reg::VCOUNT: case reg::VCOUNT + 1:
    case reg::KEYINPUT: case reg::KEYINPUT + 1:
        return;
    case reg::DISPSTAT:
        io[off] = u8((io[off] & 0x07) | (v & 0xF8));
        return;
    case reg::HALTCNT:
        io[off] = v;
        sys.cpu.halted = true;
        return;
    default:
        break;
    }
    if (off >= reg::FIFO_A && off < reg::FIFO_A + 8) {
        sys.apu.fifoWrite(off >= reg::FIFO_B ? 1 : 0, v);
        return;
    }
    if (off >= 0x90 && off < 0xA0) {
        int bank = (io[0x70] & 0x40) ? 0 : 1;
        sys.apu.wave.ram[bank * 16 + (off - 0x90)] = v;
        return;
    }

    u32 half = off & ~1u;
    u16 old = ioRead16(half);
    io[off] = v;
    u16 now = ioRead16(half);

    if (off >= 0x60 && off < 0x90) {
        sys.apu.writeReg(off, v);
        return;
    }
    switch (half) {
    case reg::BG2X: case reg::BG2X + 2: case reg::BG2X + 4: case reg::BG2X + 6:
        sys.ppu.affineRefWritten(0);
        break;
    case reg::BG3X: case reg::BG3X + 2: case reg::BG3X + 4: case reg::BG3X + 6:
        sys.ppu.affineRefWritten(1);
        break;
    case 0x0BA: sys.dma.controlWritten(0, old, now); break;
    case 0x0C6: sys.dma.controlWritten(1, old, now); break;
    case 0x0D2: sys.dma.controlWritten(2, old, now); break;
    case 0x0DE: sys.dma.controlWritten(3, old, now); break;
    case 0x102: sys.timers.writeControl(0, old, now); break;
    case 0x106: sys.timers.writeControl(1, old, now); break;
    case 0x10A: sys.timers.writeControl(2, old, now); break;
    case 0x10E: sys.timers.writeControl(3, old, now); break;
    case reg::WAITCNT: updateWaitStates(); break;
    case reg::KEYCNT: sys.checkKeypadIrq(); break;
    default: break;
    }
}

} // namespace mulator
