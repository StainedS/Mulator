// Sound: two square channels, wave channel, noise channel (legacy 8-bit PSG)
// plus the two Direct Sound FIFOs fed by DMA and clocked by timers 0/1.
#include "system.h"

namespace mulator {

static const u8 kDuty[4] = {0x01, 0x81, 0x87, 0x7E};

void Apu::reset() {
    ch[0] = ch[1] = Square{};
    noise = Noise{};
    wave = Wave{};
    fifo[0] = fifo[1] = Fifo{};
    seqCycles = seqStep = 0;
    sampleAccum = 0;
    pendingCycles = 0;
    out.clear();
    if (!cyclesPerSample) setSampleRate(48000);
}

void Apu::setSampleRate(int hz) {
    if (hz <= 0) hz = 48000;
    cyclesPerSample = (u64(CPU_CLOCK) << 16) / u64(hz);
}

u32 Apu::squareFreq(int c) const {
    return sys.bus.ioRead16(c == 0 ? 0x64 : 0x6C) & 0x7FF;
}

void Apu::writeReg(u32 addr, u8 v) {
    u8* io = sys.bus.io;
    switch (addr) {
    case 0x62: ch[0].length = 64 - (v & 63); break;
    case 0x68: ch[1].length = 64 - (v & 63); break;
    case 0x72: wave.length = 256 - v; break;
    case 0x78: noise.length = 64 - (v & 63); break;
    case 0x63: if ((v & 0xF8) == 0) ch[0].on = false; break;
    case 0x69: if ((v & 0xF8) == 0) ch[1].on = false; break;
    case 0x79: if ((v & 0xF8) == 0) noise.on = false; break;
    case 0x70: if (!(v & 0x80)) wave.on = false; break;
    case 0x65: if (v & 0x80) { trigger(0); io[0x65] &= 0x7F; } break;
    case 0x6D: if (v & 0x80) { trigger(1); io[0x6D] &= 0x7F; } break;
    case 0x75: if (v & 0x80) { trigger(2); io[0x75] &= 0x7F; } break;
    case 0x7D: if (v & 0x80) { trigger(3); io[0x7D] &= 0x7F; } break;
    case 0x83:
        if (v & 0x08) fifo[0] = Fifo{};
        if (v & 0x80) fifo[1] = Fifo{};
        io[0x83] &= 0x77;
        break;
    case 0x84:
        if (!(v & 0x80)) {
            ch[0].on = ch[1].on = wave.on = noise.on = false;
        }
        break;
    default: break;
    }
}

void Apu::trigger(int c) {
    Bus& bus = sys.bus;
    if (c < 2) {
        Square& s = ch[c];
        u16 env = bus.ioRead16(c == 0 ? 0x62 : 0x68);
        s.on = (env & 0xF800) != 0;
        s.vol = env >> 12;
        s.envTimer = (env >> 8) & 7;
        if (s.length == 0) s.length = 64;
        s.phaseCycles = 0;
        if (c == 0) {
            u8 sw = bus.io[0x60];
            int period = (sw >> 4) & 7;
            s.shadowFreq = squareFreq(0);
            s.sweepTimer = period ? period : 8;
            s.sweepOn = period || (sw & 7);
            if (sw & 7) {
                u32 delta = s.shadowFreq >> (sw & 7);
                if (!(sw & 8) && s.shadowFreq + delta > 2047) s.on = false;
            }
        }
    } else if (c == 2) {
        wave.on = (bus.io[0x70] & 0x80) != 0;
        if (wave.length == 0) wave.length = 256;
        wave.pos = 0;
        wave.phaseCycles = 0;
    } else {
        u16 env = bus.ioRead16(0x78);
        noise.on = (env & 0xF800) != 0;
        noise.vol = env >> 12;
        noise.envTimer = (env >> 8) & 7;
        if (noise.length == 0) noise.length = 64;
        noise.lfsr = (bus.io[0x7C] & 0x08) ? 0x7F : 0x7FFF;
        noise.phaseCycles = 0;
    }
}

void Apu::frameSequencer() {
    Bus& bus = sys.bus;
    int step = seqStep;
    seqStep = (seqStep + 1) & 7;

    if ((step & 1) == 0) { // length counters at 256 Hz
        if (ch[0].on && (bus.ioRead16(0x64) & 0x4000) && --ch[0].length <= 0) ch[0].on = false;
        if (ch[1].on && (bus.ioRead16(0x6C) & 0x4000) && --ch[1].length <= 0) ch[1].on = false;
        if (wave.on && (bus.ioRead16(0x74) & 0x4000) && --wave.length <= 0) wave.on = false;
        if (noise.on && (bus.ioRead16(0x7C) & 0x4000) && --noise.length <= 0) noise.on = false;
    }
    if (step == 2 || step == 6) { // frequency sweep at 128 Hz
        Square& s = ch[0];
        u8 sw = bus.io[0x60];
        if (--s.sweepTimer <= 0) {
            int period = (sw >> 4) & 7;
            s.sweepTimer = period ? period : 8;
            if (s.on && s.sweepOn && period) {
                u32 delta = s.shadowFreq >> (sw & 7);
                u32 nf = (sw & 8) ? s.shadowFreq - delta : s.shadowFreq + delta;
                if (nf > 2047) {
                    s.on = false;
                } else if (sw & 7) {
                    s.shadowFreq = nf;
                    u16 x = bus.ioRead16(0x64);
                    bus.ioSet16(0x64, u16((x & ~0x7FF) | nf));
                    if (nf + (nf >> (sw & 7)) > 2047 && !(sw & 8)) s.on = false;
                }
            }
        }
    }
    if (step == 7) { // volume envelopes at 64 Hz
        auto env = [&](int& vol, int& timer, u16 reg) {
            int period = (reg >> 8) & 7;
            if (!period) return;
            if (--timer <= 0) {
                timer = period;
                if ((reg & 0x0800) && vol < 15) vol++;
                else if (!(reg & 0x0800) && vol > 0) vol--;
            }
        };
        env(ch[0].vol, ch[0].envTimer, bus.ioRead16(0x62));
        env(ch[1].vol, ch[1].envTimer, bus.ioRead16(0x68));
        env(noise.vol, noise.envTimer, bus.ioRead16(0x78));
    }
}

void Apu::advanceChannels(int cycles) {
    Bus& bus = sys.bus;
    for (int c = 0; c < 2; c++) {
        Square& s = ch[c];
        if (!s.on) continue;
        u32 period = (2048 - squareFreq(c)) * 16;
        s.phaseCycles += u32(cycles);
        if (s.phaseCycles >= period) {
            u32 steps = s.phaseCycles / period;
            s.phaseCycles -= steps * period;
            s.dutyPos = int((s.dutyPos + steps) & 7);
        }
    }
    if (wave.on) {
        u32 period = (2048 - (bus.ioRead16(0x74) & 0x7FF)) * 8;
        wave.phaseCycles += u32(cycles);
        if (wave.phaseCycles >= period) {
            u32 steps = wave.phaseCycles / period;
            wave.phaseCycles -= steps * period;
            int len = (bus.io[0x70] & 0x20) ? 64 : 32;
            wave.pos = int((wave.pos + steps) % u32(len));
        }
    }
    if (noise.on) {
        u16 cnt = bus.ioRead16(0x7C);
        u32 r = cnt & 7, sh = (cnt >> 4) & 15;
        u32 period = (r ? 32 * r : 16) << (sh + 1);
        noise.phaseCycles += u32(cycles);
        bool narrow = cnt & 0x08;
        while (noise.phaseCycles >= period) {
            noise.phaseCycles -= period;
            u16 bit = (noise.lfsr ^ (noise.lfsr >> 1)) & 1;
            noise.lfsr = u16(noise.lfsr >> 1);
            if (narrow) noise.lfsr = u16((noise.lfsr & ~0x40) | (bit << 6));
            else noise.lfsr = u16((noise.lfsr & ~0x4000) | (bit << 14));
        }
    }
}

void Apu::generateSample() {
    Bus& bus = sys.bus;
    if (!(bus.io[reg::SOUNDCNT_X] & 0x80)) {
        out.push_back(0);
        out.push_back(0);
        return;
    }
    u16 cntL = bus.ioRead16(reg::SOUNDCNT_L);
    u16 cntH = bus.ioRead16(reg::SOUNDCNT_H);

    int psg[4] = {0, 0, 0, 0};
    for (int c = 0; c < 2; c++) {
        if (!ch[c].on) continue;
        int duty = (bus.io[c == 0 ? 0x62 : 0x68] >> 6) & 3;
        bool high = (kDuty[duty] >> (7 - ch[c].dutyPos)) & 1;
        psg[c] = high ? ch[c].vol : -ch[c].vol;
    }
    if (wave.on) {
        int sel = (bus.io[0x70] >> 6) & 1;
        int pos = wave.pos;
        int bank = sel;
        if (bus.io[0x70] & 0x20) { bank = sel ^ (pos >= 32 ? 1 : 0); pos &= 31; }
        u8 byte = wave.ram[bank * 16 + pos / 2];
        int nib = (pos & 1) ? (byte & 15) : (byte >> 4);
        int s = (nib - 8) * 2;
        u16 wv = bus.ioRead16(0x72);
        if (wv & 0x8000) s = s * 3 / 4;
        else {
            switch ((wv >> 13) & 3) {
            case 0: s = 0; break;
            case 1: break;
            case 2: s /= 2; break;
            case 3: s /= 4; break;
            }
        }
        psg[2] = s;
    }
    if (noise.on) psg[3] = (noise.lfsr & 1) ? -noise.vol : noise.vol;

    int left = 0, right = 0;
    for (int c = 0; c < 4; c++) {
        if (cntL & (0x100 << c)) right += psg[c];
        if (cntL & (0x1000 << c)) left += psg[c];
    }
    right *= (cntL & 7) + 1;
    left *= ((cntL >> 4) & 7) + 1;
    int psgShift = 2 - (cntH & 3);
    if (psgShift < 0) psgShift = 0;
    right >>= psgShift;
    left >>= psgShift;

    int a = fifo[0].current * ((cntH & 0x04) ? 4 : 2);
    int b = fifo[1].current * ((cntH & 0x08) ? 4 : 2);
    if (cntH & 0x0100) right += a;
    if (cntH & 0x0200) left += a;
    if (cntH & 0x1000) right += b;
    if (cntH & 0x2000) left += b;

    auto clamp = [](int v) { return s16(v > 32767 ? 32767 : v < -32768 ? -32768 : v); };
    out.push_back(clamp(left * 20));
    out.push_back(clamp(right * 20));
}

void Apu::tick(int cycles) {
    seqCycles += cycles;
    while (seqCycles >= 32768) {
        seqCycles -= 32768;
        frameSequencer();
    }
    pendingCycles += cycles;
    sampleAccum += u64(cycles) << 16;
    while (sampleAccum >= cyclesPerSample) {
        sampleAccum -= cyclesPerSample;
        advanceChannels(pendingCycles);
        pendingCycles = 0;
        generateSample();
    }
    // Drop audio nobody is reading so memory cannot grow without bound.
    if (out.size() > 48000 * 2) out.erase(out.begin(), out.begin() + (out.size() - 24000 * 2));
}

void Apu::fifoWrite(int f, u8 v) {
    Fifo& q = fifo[f];
    if (q.count >= 32) return;
    q.buf[q.wr] = s8(v);
    q.wr = (q.wr + 1) & 31;
    q.count++;
}

void Apu::timerOverflow(int timer) {
    u16 cntH = sys.bus.ioRead16(reg::SOUNDCNT_H);
    for (int f = 0; f < 2; f++) {
        int sel = (cntH >> (f == 0 ? 10 : 14)) & 1;
        if (sel != timer) continue;
        Fifo& q = fifo[f];
        if (q.count > 0) {
            q.current = q.buf[q.rd];
            q.rd = (q.rd + 1) & 31;
            q.count--;
        }
        if (q.count <= 16) sys.dma.fifoRequest(f);
    }
}

size_t Apu::read(s16* dst, size_t maxFrames) {
    size_t frames = out.size() / 2;
    if (frames > maxFrames) frames = maxFrames;
    std::memcpy(dst, out.data(), frames * 2 * sizeof(s16));
    out.erase(out.begin(), out.begin() + frames * 2);
    return frames;
}

} // namespace mulator
