// Hardware timers (4, cascadable) and DMA controller (4 channels).
#include <climits>

#include "system.h"

namespace mulator {

// ---------------------------------------------------------------- Timers
static const int kPrescalerShift[4] = {0, 6, 8, 10};

void Timers::reset() {
    for (auto& t : t_) t = T{};
    running = 0;
}

u16 Timers::reload(int t) const { return sys.bus.ioRead16(reg::TM0CNT + 4 * t); }
u16 Timers::control(int t) const { return sys.bus.ioRead16(reg::TM0CNT + 4 * t + 2); }

void Timers::writeControl(int t, u16 oldCtrl, u16 newCtrl) {
    if (!(oldCtrl & 0x80) && (newCtrl & 0x80)) {
        t_[t].counter = reload(t);
        t_[t].prescAccum = 0;
    }
    bool cascade = t > 0 && (newCtrl & 0x04);
    if ((newCtrl & 0x80) && !cascade) running |= u8(1 << t);
    else running &= u8(~(1 << t));
}

void Timers::tick(int cycles) {
    if (!running) return;
    for (int t = 0; t < 4; t++) {
        if (!(running & (1 << t))) continue;
        T& tm = t_[t];
        int sh = kPrescalerShift[control(t) & 3];
        tm.prescAccum += u32(cycles);
        u32 inc = tm.prescAccum >> sh;
        tm.prescAccum &= (1u << sh) - 1;
        if (!inc) continue;
        tm.counter += inc;
        if (tm.counter >= 0x10000) {
            u32 rl = reload(t);
            u32 period = 0x10000 - rl;
            u32 excess = tm.counter - 0x10000;
            u32 times = 1 + excess / period;
            tm.counter = rl + excess % period;
            overflow(t, times);
        }
    }
}

void Timers::overflow(int t, u32 times) {
    u16 ctrl = control(t);
    if (ctrl & 0x40) sys.requestIrq(u16(IRQ_TIMER0 << t));
    if (t < 2) {
        u32 n = times > 32 ? 32 : times;
        for (u32 i = 0; i < n; i++) sys.apu.timerOverflow(t);
    }
    if (t < 3) {
        u16 next = control(t + 1);
        if ((next & 0x84) == 0x84) {
            T& nt = t_[t + 1];
            nt.counter += times;
            if (nt.counter >= 0x10000) {
                u32 rl = reload(t + 1);
                u32 period = 0x10000 - rl;
                u32 excess = nt.counter - 0x10000;
                u32 nTimes = 1 + excess / period;
                nt.counter = rl + excess % period;
                overflow(t + 1, nTimes);
            }
        }
    }
}

int Timers::cyclesUntilOverflow() const {
    int best = INT_MAX;
    for (int t = 0; t < 4; t++) {
        if (!(running & (1 << t))) continue;
        int sh = kPrescalerShift[control(t) & 3];
        s64 c = (s64(0x10000 - t_[t].counter) << sh) - t_[t].prescAccum;
        if (c < best) best = int(c);
    }
    return best;
}

// ---------------------------------------------------------------- DMA
void Dma::reset() {
    for (auto& ch : c) ch = Channel{};
}

void Dma::controlWritten(int ch, u16 oldCtrl, u16 newCtrl) {
    if ((oldCtrl & 0x8000) || !(newCtrl & 0x8000)) return;
    u32 base = reg::DMA0SAD + 12 * ch;
    c[ch].src = rd32(sys.bus.io + base) & (ch == 0 ? 0x07FFFFFFu : 0x0FFFFFFFu);
    c[ch].dst = rd32(sys.bus.io + base + 4) & (ch == 3 ? 0x0FFFFFFFu : 0x07FFFFFFu);
    u32 cnt = rd16(sys.bus.io + base + 8);
    if (ch == 3) c[ch].count = cnt ? cnt : 0x10000;
    else c[ch].count = (cnt & 0x3FFF) ? (cnt & 0x3FFF) : 0x4000;
    if (((newCtrl >> 12) & 3) == 0) run(ch);
}

void Dma::trigger(int timing) {
    for (int ch = 0; ch < 4; ch++) {
        u16 ctrl = sys.bus.ioRead16(0xBA + 12 * ch);
        if ((ctrl & 0x8000) && ((ctrl >> 12) & 3) == u32(timing)) run(ch);
    }
}

void Dma::fifoRequest(int fifo) {
    u32 addr = 0x040000A0u + 4 * fifo;
    for (int ch = 1; ch <= 2; ch++) {
        u16 ctrl = sys.bus.ioRead16(0xBA + 12 * ch);
        if ((ctrl & 0x8000) && ((ctrl >> 12) & 3) == 3 && c[ch].dst == addr) run(ch);
    }
}

void Dma::run(int ch) {
    Bus& bus = sys.bus;
    u32 ctrlAddr = 0xBA + 12 * ch;
    u16 ctrl = bus.ioRead16(ctrlAddr);
    int timing = (ctrl >> 12) & 3;
    bool fifoMode = timing == 3 && (ch == 1 || ch == 2);
    bool wide = fifoMode || (ctrl & 0x400);
    int dstCtl = (ctrl >> 5) & 3, srcCtl = (ctrl >> 7) & 3;
    u32 count = fifoMode ? 4 : c[ch].count;
    int unit = wide ? 4 : 2;
    int srcStep = srcCtl == 1 ? -unit : srcCtl == 2 ? 0 : unit;
    int dstStep = fifoMode ? 0 : dstCtl == 1 ? -unit : dstCtl == 2 ? 0 : unit;

    if (ch == 3 && sys.cart.isEepromAddr(c[ch].dst)) sys.cart.eepromSetSizeFromDma(count);
    if (ch == 3 && sys.cart.isEepromAddr(c[ch].src)) sys.cart.eepromSetSizeFromDma(count);

    u32 src = c[ch].src, dst = c[ch].dst;
    bool seq = false;
    for (u32 i = 0; i < count; i++) {
        if (wide) {
            u32 v = bus.read32(src & ~3u, seq);
            bus.write32(dst & ~3u, v, seq);
        } else {
            u16 v = bus.read16(src & ~1u, seq);
            bus.write16(dst & ~1u, v, seq);
        }
        seq = true;
        src += u32(srcStep);
        dst += u32(dstStep);
    }
    bus.cycles += 2;
    c[ch].src = src;
    c[ch].dst = dst;

    if (ctrl & 0x4000) sys.requestIrq(u16(IRQ_DMA0 << ch));
    if ((ctrl & 0x200) && timing != 0) {
        if (!fifoMode) {
            u32 cnt = bus.ioRead16(0xB8 + 12 * ch);
            if (ch == 3) c[ch].count = cnt ? cnt : 0x10000;
            else c[ch].count = (cnt & 0x3FFF) ? (cnt & 0x3FFF) : 0x4000;
            if (dstCtl == 3)
                c[ch].dst = rd32(bus.io + 0xB4 + 12 * ch) & (ch == 3 ? 0x0FFFFFFFu : 0x07FFFFFFu);
        }
    } else {
        bus.ioSet16(ctrlAddr, ctrl & 0x7FFF);
    }
}

} // namespace mulator
