// Internal declarations of every emulated hardware component.
#pragma once
#include <string>
#include <vector>

#include "common.h"

namespace mulator {

class System;

// ---------------------------------------------------------------- CPU (ARM7TDMI)
class Cpu {
public:
    explicit Cpu(System& s) : sys(s) {}
    void reset(bool skipBios);
    int step();               // executes one instruction, returns cycles used
    void raiseIrq();          // enters IRQ exception if allowed
    bool irqEnabled() const { return !(cpsr & FLAG_I); }

    template <class S> void serialize(S& s) {
        s.pod(r); s.pod(cpsr); s.pod(bankR13); s.pod(bankR14); s.pod(bankSpsr);
        s.pod(fiqRegs); s.pod(usrRegs); s.pod(halted); s.pod(intrWaitActive);
        s.pod(nextFetchSeq); s.pod(pipe); s.pod(curAddr);
    }

    static constexpr u32 FLAG_N = 1u << 31, FLAG_Z = 1u << 30, FLAG_C = 1u << 29, FLAG_V = 1u << 28;
    static constexpr u32 FLAG_I = 1u << 7, FLAG_F = 1u << 6, FLAG_T = 1u << 5;
    enum Mode : u32 { USR = 0x10, FIQ = 0x11, IRQ = 0x12, SVC = 0x13, ABT = 0x17, UND = 0x1B, SYS = 0x1F };

    u32 r[16] = {};
    u32 cpsr = 0;
    u32 bankR13[6] = {}, bankR14[6] = {}, bankSpsr[6] = {};
    u32 fiqRegs[5] = {}, usrRegs[5] = {};
    bool halted = false;
    bool intrWaitActive = false;
    bool nextFetchSeq = false;
    u32 curAddr = 0;   // address of the instruction being executed
    u32 pipe[2] = {};  // prefetched opcodes (decode, fetch)
    void refillPipeline();

    bool thumb() const { return cpsr & FLAG_T; }
    u32 nextInstrAddr() const { return r[15] - (thumb() ? 4 : 8); }
    void branchTo(u32 addr);
    void setCpsr(u32 v);
    void switchMode(u32 mode);
    u32& spsr();

private:
    System& sys;
    bool flushed = false;
    int cycles = 0;

    static int bankIndex(u32 mode);
    bool condPassed(u32 cond) const;
    void setNZ(u32 v) { cpsr = (cpsr & ~(FLAG_N | FLAG_Z)) | (v & FLAG_N) | (v ? 0 : FLAG_Z); }
    void setC(bool c) { cpsr = c ? (cpsr | FLAG_C) : (cpsr & ~FLAG_C); }
    void setV(bool v) { cpsr = v ? (cpsr | FLAG_V) : (cpsr & ~FLAG_V); }
    bool flagC() const { return cpsr & FLAG_C; }

    u32 add(u32 a, u32 b, bool s);
    u32 adc(u32 a, u32 b, bool s);
    u32 sub(u32 a, u32 b, bool s);
    u32 sbc(u32 a, u32 b, bool s);
    u32 shiftImm(int type, u32 v, int amt, bool& c);
    u32 shiftReg(int type, u32 v, u32 amt, bool& c);
    void mulCycles(u32 rs, bool isSigned);

    void execArm(u32 op);
    void armDataProc(u32 op);
    void armMul(u32 op);
    void armMull(u32 op);
    void armSwap(u32 op);
    void armHalfTransfer(u32 op);
    void armSingleTransfer(u32 op);
    void armBlockTransfer(u32 op);
    void armMrs(u32 op);
    void armMsr(u32 op);
    void execThumb(u16 op);
    void thumbAlu(u16 op);
    void thumbHiReg(u16 op);
    void thumbBlock(u32 base, u32 rlist, bool load, bool writeback, int rb);
    void swi(u32 comment);
    void writeReg(int n, u32 v) { if (n == 15) branchTo(v); else r[n] = v; }

    u32 read32(u32 a, bool seq = false);
    u32 read16(u32 a, bool seq = false);
    u32 read8(u32 a, bool seq = false);
    void write32(u32 a, u32 v, bool seq = false);
    void write16(u32 a, u32 v, bool seq = false);
    void write8(u32 a, u32 v, bool seq = false);
};

// ---------------------------------------------------------------- Cartridge
enum class SaveType : u8 { None, Sram, Flash64, Flash128, Eeprom };

class Cart {
public:
    std::vector<u8> rom;
    std::vector<u8> save;
    SaveType saveType = SaveType::None;
    bool dirty = false;

    void load(const u8* data, size_t size);
    void detectSaveType();
    void resetState();
    std::string title() const;
    std::string gameCode() const;

    u8 readBackup(u32 addr);
    void writeBackup(u32 addr, u8 v);

    bool isEepromAddr(u32 addr) const;
    void eepromSetSizeFromDma(u32 count);
    u16 eepromRead();
    void eepromWrite(u16 v);

    // Cartridge GPIO port at 0x080000C4-0x080000C9 with a Seiko S-3511 real-time clock
    // (used by several games and many ROM hacks).
    bool gpioRead(u32 addr, u16& v) const;
    void gpioWrite(u32 addr, u16 v);

    template <class S> void serialize(S& s) {
        s.vec(save); s.pod(saveType); s.pod(flash); s.pod(eeprom); s.pod(gpio); s.pod(rtc);
    }

    struct FlashState {
        u8 state = 0;
        u8 bank = 0;
        bool idMode = false;
        bool erasePrep = false;
        bool writeNext = false;
        bool bankNext = false;
    } flash;

    struct EepromState {
        int addrBits = 0; // 0 = unknown, 6 = 512 B, 14 = 8 KiB
        int mode = 0;     // 0 idle, 1 receiving command, 2 sending data
        u64 bits = 0;
        int bitCount = 0;
        int cmd = 0;
        u32 address = 0;
        u64 data = 0;
        int readPos = 0;
    } eeprom;

    struct GpioState {
        u8 pins = 0;      // bit 0 SCK, bit 1 SIO, bit 2 CS
        u8 direction = 0; // 1 = pin driven by the console
        u8 readable = 0;  // 1 = the port registers are visible instead of ROM
    } gpio;

    struct RtcState {
        int step = 0;
        int bitsRead = 0;
        u8 bits = 0;
        u8 command = 0;
        int bytesLeft = 0;
        bool active = false;
        u8 control = 0x40; // 24-hour mode
        u8 time[7] = {};
    } rtc;

private:
    void eepromCommit();
    void rtcPins();
    void rtcByte();
    void rtcLatchTime();
    void gpioOutput(u8 pins);
};

// ---------------------------------------------------------------- PPU
class Ppu {
public:
    explicit Ppu(System& s) : sys(s) {}
    void reset();
    void renderLine(int line);
    void vblankLatch();
    void affineRefWritten(int bg);
    void lineDone();
    u32 framebuffer[240 * 160] = {};

    template <class S> void serialize(S& s) { s.pod(refX); s.pod(refY); }

private:
    System& sys;
    s32 refX[2] = {}, refY[2] = {};

    u16 bgLine[4][240];
    u16 objColor[240];
    u8 objPrio[240];
    bool objSemi[240];
    bool objWin[240];

    void renderTextBg(int bg, int line);
    void renderAffineBg(int bg);
    void renderBitmapBg(int mode);
    void renderObjs(int line);
    void compose(int line);
};

// ---------------------------------------------------------------- APU
class Apu {
public:
    explicit Apu(System& s) : sys(s) {}
    void reset();
    void tick(int cycles);
    void writeReg(u32 addr, u8 value);   // after raw io[] store
    void fifoWrite(int fifo, u8 value);
    void timerOverflow(int timer);
    void setSampleRate(int hz);
    size_t read(s16* out, size_t maxFrames);

    template <class S> void serialize(S& s) {
        s.pod(ch); s.pod(noise); s.pod(wave); s.pod(fifo); s.pod(seqCycles); s.pod(seqStep);
        s.pod(sampleAccum); s.pod(pendingCycles);
    }

    struct Square {
        bool on = false;
        int vol = 0, envTimer = 0, length = 0;
        u32 phaseCycles = 0;
        int duty = 0, dutyPos = 0;
        int sweepTimer = 0;
        u32 shadowFreq = 0;
        bool sweepOn = false;
    } ch[2];
    struct Noise {
        bool on = false;
        int vol = 0, envTimer = 0, length = 0;
        u32 phaseCycles = 0;
        u16 lfsr = 0x7FFF;
    } noise;
    struct Wave {
        bool on = false;
        int length = 0;
        u32 phaseCycles = 0;
        int pos = 0;
        u8 ram[32] = {};
    } wave;
    struct Fifo {
        s8 buf[32] = {};
        int rd = 0, wr = 0, count = 0;
        s8 current = 0;
    } fifo[2];

private:
    System& sys;
    int seqCycles = 0, seqStep = 0;
    u64 sampleAccum = 0;          // fixed point 32.16 cycles
    u64 cyclesPerSample = 0;      // fixed point 32.16
    int pendingCycles = 0;        // cycles not yet applied to the channel oscillators
    std::vector<s16> out;

    void frameSequencer();
    void advanceChannels(int cycles);
    void generateSample();
    void trigger(int channel);
    u32 squareFreq(int c) const;
};

// ---------------------------------------------------------------- Timers
class Timers {
public:
    explicit Timers(System& s) : sys(s) {}
    void reset();
    void tick(int cycles);
    int cyclesUntilOverflow() const;
    u16 readCounter(int t) const { return u16(t_[t].counter); }
    void writeControl(int t, u16 oldCtrl, u16 newCtrl);
    bool anyRunning() const { return running != 0; }

    template <class S> void serialize(S& s) { s.pod(t_); s.pod(running); }

private:
    System& sys;
    struct T {
        u32 counter = 0;
        u32 prescAccum = 0;
    } t_[4];
    u8 running = 0;
    void overflow(int t, u32 times);
    u16 reload(int t) const;
    u16 control(int t) const;
};

// ---------------------------------------------------------------- DMA
class Dma {
public:
    explicit Dma(System& s) : sys(s) {}
    void reset();
    void controlWritten(int ch, u16 oldCtrl, u16 newCtrl);
    void trigger(int timing);       // 1 = vblank, 2 = hblank
    void fifoRequest(int fifo);     // sound FIFO A/B wants data

    template <class S> void serialize(S& s) { s.pod(c); }

private:
    System& sys;
    struct Channel {
        u32 src = 0, dst = 0, count = 0;
    } c[4];
    void run(int ch);
};

// ---------------------------------------------------------------- Memory bus
class Bus {
public:
    explicit Bus(System& s) : sys(s) {}
    void reset();

    u32 read32(u32 addr, bool seq);
    u16 read16(u32 addr, bool seq);
    u8 read8(u32 addr, bool seq);
    void write32(u32 addr, u32 v, bool seq);
    void write16(u32 addr, u16 v, bool seq);
    void write8(u32 addr, u8 v, bool seq);
    u32 fetch32(u32 addr, bool seq);
    u16 fetch16(u32 addr, bool seq);

    // Raw access (no timing, no side effects beyond the write itself), used by HLE BIOS.
    u32 peek32(u32 addr) { return readRaw32(addr); }

    int cycles = 0;        // accumulated wait cycles of the current CPU step
    u32 openBus = 0;
    u32 biosLatch = 0xE129F000;
    bool hasBios = false;

    u8 bios[0x4000] = {};
    u8 ewram[0x40000] = {};
    u8 iwram[0x8000] = {};
    u8 io[0x400] = {};
    u8 pal[0x400] = {};
    u8 vram[0x18000] = {};
    u8 oam[0x400] = {};

    u16 ioRead16(u32 off) const { return rd16(io + off); }
    void ioSet16(u32 off, u16 v) { wr16(io + off, v); }
    void updateWaitStates();

    template <class S> void serialize(S& s) {
        s.pod(ewram); s.pod(iwram); s.pod(io); s.pod(pal); s.pod(vram); s.pod(oam);
        s.pod(openBus); s.pod(biosLatch);
    }

private:
    System& sys;
    u8 waitN16[16], waitS16[16], waitN32[16], waitS32[16];
    u32 readRaw32(u32 addr);
    u16 readRaw16(u32 addr);
    u8 readRaw8(u32 addr);
    void writeRaw8(u32 addr, u8 v);
    void writeRaw16(u32 addr, u16 v);
    void writeRaw32(u32 addr, u32 v);
    u8 ioRead8(u32 off);
    void ioWrite8(u32 off, u8 v);
    u32 romRead16(u32 addr);
    void addWait(u32 addr, bool seq, bool wide, bool code);
};

// ---------------------------------------------------------------- System
class System {
public:
    System();
    Cpu cpu;
    Bus bus;
    Ppu ppu;
    Apu apu;
    Timers timers;
    Dma dma;
    Cart cart;

    u16 keys = 0;           // pressed keys bitmask
    int cycleDebt = 0;
    bool frameDone = false;

    void reset();
    void runFrame();
    void requestIrq(u16 bits);
    void checkKeypadIrq();
    bool hleSwi(u32 number);   // returns true if handled

    template <class S> void serialize(S& s) {
        cpu.serialize(s); bus.serialize(s); ppu.serialize(s); apu.serialize(s);
        timers.serialize(s); dma.serialize(s); cart.serialize(s);
        s.pod(cycleDebt);
    }

private:
    void runCycles(int n);
    void startLine(int line);
    void hblank(int line);
};

// IO register offsets used across components.
namespace reg {
constexpr u32 DISPCNT = 0x000, DISPSTAT = 0x004, VCOUNT = 0x006;
constexpr u32 BG0CNT = 0x008, BG0HOFS = 0x010;
constexpr u32 BG2PA = 0x020, BG2X = 0x028, BG3PA = 0x030, BG3X = 0x038;
constexpr u32 WIN0H = 0x040, WIN1H = 0x042, WIN0V = 0x044, WIN1V = 0x046, WININ = 0x048, WINOUT = 0x04A;
constexpr u32 MOSAIC = 0x04C, BLDCNT = 0x050, BLDALPHA = 0x052, BLDY = 0x054;
constexpr u32 SOUNDCNT_L = 0x080, SOUNDCNT_H = 0x082, SOUNDCNT_X = 0x084, SOUNDBIAS = 0x088;
constexpr u32 FIFO_A = 0x0A0, FIFO_B = 0x0A4;
constexpr u32 DMA0SAD = 0x0B0;
constexpr u32 TM0CNT = 0x100;
constexpr u32 KEYINPUT = 0x130, KEYCNT = 0x132;
constexpr u32 IE = 0x200, IF = 0x202, WAITCNT = 0x204, IME = 0x208, POSTFLG = 0x300, HALTCNT = 0x301;
} // namespace reg

} // namespace mulator
