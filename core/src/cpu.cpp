// ARM7TDMI interpreter (ARMv4T: ARM + Thumb instruction sets).
//
// Pipeline model: while an instruction executes, r[15] holds its address + 8
// (ARM) or + 4 (Thumb), exactly what the program observes when reading PC.
#include "system.h"

namespace mulator {

static int popcount16(u32 v) {
    int n = 0;
    for (; v; v &= v - 1) n++;
    return n;
}

int Cpu::bankIndex(u32 mode) {
    switch (mode) {
    case FIQ: return 1;
    case IRQ: return 2;
    case SVC: return 3;
    case ABT: return 4;
    case UND: return 5;
    default: return 0;
    }
}

void Cpu::reset(bool skipBios) {
    std::memset(r, 0, sizeof(r));
    std::memset(bankR13, 0, sizeof(bankR13));
    std::memset(bankR14, 0, sizeof(bankR14));
    std::memset(bankSpsr, 0, sizeof(bankSpsr));
    std::memset(fiqRegs, 0, sizeof(fiqRegs));
    std::memset(usrRegs, 0, sizeof(usrRegs));
    halted = false;
    intrWaitActive = false;
    nextFetchSeq = false;
    if (skipBios) {
        cpsr = SYS;
        r[13] = 0x03007F00;
        bankR13[bankIndex(IRQ)] = 0x03007FA0;
        bankR13[bankIndex(SVC)] = 0x03007FE0;
        branchTo(0x08000000);
    } else {
        cpsr = SVC | FLAG_I | FLAG_F;
        branchTo(0);
    }
    refillPipeline();
}

void Cpu::refillPipeline() {
    if (thumb()) {
        pipe[0] = sys.bus.fetch16(r[15] - 4, false);
        pipe[1] = sys.bus.fetch16(r[15] - 2, true);
    } else {
        pipe[0] = sys.bus.fetch32(r[15] - 8, false);
        pipe[1] = sys.bus.fetch32(r[15] - 4, true);
    }
    nextFetchSeq = true;
}

void Cpu::switchMode(u32 mode) {
    int ob = bankIndex(cpsr & 0x1F), nb = bankIndex(mode);
    if (ob != nb) {
        bankR13[ob] = r[13];
        bankR14[ob] = r[14];
        if (ob == 1) {
            for (int i = 0; i < 5; i++) { fiqRegs[i] = r[8 + i]; r[8 + i] = usrRegs[i]; }
        } else if (nb == 1) {
            for (int i = 0; i < 5; i++) { usrRegs[i] = r[8 + i]; r[8 + i] = fiqRegs[i]; }
        }
        r[13] = bankR13[nb];
        r[14] = bankR14[nb];
    }
    cpsr = (cpsr & ~0x1Fu) | mode;
}

void Cpu::setCpsr(u32 v) {
    switchMode(v & 0x1F);
    cpsr = v;
}

u32& Cpu::spsr() { return bankSpsr[bankIndex(cpsr & 0x1F)]; }

void Cpu::branchTo(u32 addr) {
    if (thumb()) r[15] = (addr & ~1u) + 4;
    else r[15] = (addr & ~3u) + 8;
    flushed = true;
}

bool Cpu::condPassed(u32 cond) const {
    bool n = cpsr & FLAG_N, z = cpsr & FLAG_Z, c = cpsr & FLAG_C, v = cpsr & FLAG_V;
    switch (cond & 15) {
    case 0x0: return z;
    case 0x1: return !z;
    case 0x2: return c;
    case 0x3: return !c;
    case 0x4: return n;
    case 0x5: return !n;
    case 0x6: return v;
    case 0x7: return !v;
    case 0x8: return c && !z;
    case 0x9: return !c || z;
    case 0xA: return n == v;
    case 0xB: return n != v;
    case 0xC: return !z && n == v;
    case 0xD: return z || n != v;
    case 0xE: return true;
    default: return false;
    }
}

// ---------------------------------------------------------------- memory helpers
u32 Cpu::read32(u32 a, bool seq) { nextFetchSeq = false; return sys.bus.read32(a, seq); }
u32 Cpu::read16(u32 a, bool seq) { nextFetchSeq = false; return sys.bus.read16(a, seq); }
u32 Cpu::read8(u32 a, bool seq) { nextFetchSeq = false; return sys.bus.read8(a, seq); }
void Cpu::write32(u32 a, u32 v, bool seq) { nextFetchSeq = false; sys.bus.write32(a, v, seq); }
void Cpu::write16(u32 a, u32 v, bool seq) { nextFetchSeq = false; sys.bus.write16(a, u16(v), seq); }
void Cpu::write8(u32 a, u32 v, bool seq) { nextFetchSeq = false; sys.bus.write8(a, u8(v), seq); }

// ---------------------------------------------------------------- execution loop
// Two-stage prefetch: pipe[0] is the instruction being executed, pipe[1] the
// next one. The fetch of the instruction after that happens before execution,
// so code that overwrites the next two instructions still runs the old ones.
int Cpu::step() {
    cycles = 0;
    flushed = false;
    if (thumb()) {
        curAddr = r[15] - 4;
        u16 op = u16(pipe[0]);
        pipe[0] = pipe[1];
        pipe[1] = sys.bus.fetch16(r[15], nextFetchSeq);
        nextFetchSeq = true;
        execThumb(op);
        if (!flushed) r[15] += 2;
    } else {
        curAddr = r[15] - 8;
        u32 op = pipe[0];
        pipe[0] = pipe[1];
        pipe[1] = sys.bus.fetch32(r[15], nextFetchSeq);
        nextFetchSeq = true;
        if (condPassed(op >> 28)) execArm(op);
        if (!flushed) r[15] += 4;
    }
    if (flushed) refillPipeline();
    int total = cycles + sys.bus.cycles;
    sys.bus.cycles = 0;
    return total;
}

void Cpu::raiseIrq() {
    u32 ret = nextInstrAddr() + 4;
    u32 old = cpsr;
    switchMode(IRQ);
    spsr() = old;
    r[14] = ret;
    cpsr = (cpsr & ~FLAG_T) | FLAG_I;
    branchTo(0x18);
    refillPipeline();
    halted = false;
}

void Cpu::swi(u32 comment) {
    if (!sys.bus.hasBios && sys.hleSwi(comment)) {
        sys.bus.biosLatch = 0xE3A02004; // what the real BIOS leaves prefetched after a SWI
        return;
    }
    u32 ret = curAddr + (thumb() ? 2 : 4);
    u32 old = cpsr;
    switchMode(SVC);
    spsr() = old;
    r[14] = ret;
    cpsr = (cpsr & ~FLAG_T) | FLAG_I;
    branchTo(0x08);
}

// ---------------------------------------------------------------- ALU helpers
u32 Cpu::add(u32 a, u32 b, bool s) {
    u32 res = a + b;
    if (s) {
        setNZ(res);
        setC(res < a);
        setV(((~(a ^ b)) & (a ^ res)) >> 31);
    }
    return res;
}

u32 Cpu::adc(u32 a, u32 b, bool s) {
    u64 wide = u64(a) + b + (flagC() ? 1 : 0);
    u32 res = u32(wide);
    if (s) {
        setNZ(res);
        setC(wide >> 32);
        setV(((~(a ^ b)) & (a ^ res)) >> 31);
    }
    return res;
}

u32 Cpu::sub(u32 a, u32 b, bool s) {
    u32 res = a - b;
    if (s) {
        setNZ(res);
        setC(a >= b);
        setV(((a ^ b) & (a ^ res)) >> 31);
    }
    return res;
}

u32 Cpu::sbc(u32 a, u32 b, bool s) {
    u64 subtrahend = u64(b) + (flagC() ? 0 : 1);
    u32 res = u32(u64(a) - subtrahend);
    if (s) {
        setNZ(res);
        setC(u64(a) >= subtrahend);
        setV(((a ^ b) & (a ^ res)) >> 31);
    }
    return res;
}

u32 Cpu::shiftImm(int type, u32 v, int amt, bool& c) {
    switch (type) {
    case 0: // LSL
        if (amt == 0) return v;
        c = (v >> (32 - amt)) & 1;
        return v << amt;
    case 1: // LSR (#0 means #32)
        if (amt == 0) { c = v >> 31; return 0; }
        c = (v >> (amt - 1)) & 1;
        return v >> amt;
    case 2: // ASR (#0 means #32)
        if (amt == 0) { c = v >> 31; return u32(s32(v) >> 31); }
        c = (v >> (amt - 1)) & 1;
        return u32(s32(v) >> amt);
    default: // ROR (#0 means RRX)
        if (amt == 0) {
            bool nc = v & 1;
            u32 res = (v >> 1) | (u32(c) << 31);
            c = nc;
            return res;
        }
        c = (v >> (amt - 1)) & 1;
        return ror32(v, amt);
    }
}

u32 Cpu::shiftReg(int type, u32 v, u32 amt, bool& c) {
    if (amt == 0) return v;
    switch (type) {
    case 0:
        if (amt < 32) { c = (v >> (32 - amt)) & 1; return v << amt; }
        c = (amt == 32) ? (v & 1) : 0;
        return 0;
    case 1:
        if (amt < 32) { c = (v >> (amt - 1)) & 1; return v >> amt; }
        c = (amt == 32) ? (v >> 31) : 0;
        return 0;
    case 2:
        if (amt < 32) { c = (v >> (amt - 1)) & 1; return u32(s32(v) >> amt); }
        c = v >> 31;
        return u32(s32(v) >> 31);
    default:
        amt &= 31;
        if (amt == 0) { c = v >> 31; return v; }
        c = (v >> (amt - 1)) & 1;
        return ror32(v, amt);
    }
}

void Cpu::mulCycles(u32 rs, bool isSigned) {
    u32 m = rs;
    if (isSigned && (m >> 31)) m = ~m;
    if ((m >> 8) == 0) cycles += 1;
    else if ((m >> 16) == 0) cycles += 2;
    else if ((m >> 24) == 0) cycles += 3;
    else cycles += 4;
}

// ---------------------------------------------------------------- ARM
void Cpu::execArm(u32 op) {
    switch ((op >> 25) & 7) {
    case 0:
        if ((op & 0x0FFFFFF0) == 0x012FFF10) { // BX
            u32 target = r[op & 15];
            if (target & 1) cpsr |= FLAG_T;
            else cpsr &= ~FLAG_T;
            branchTo(target);
            return;
        }
        if ((op & 0x90) == 0x90) {
            if ((op & 0x0FC000F0) == 0x00000090) return armMul(op);
            if ((op & 0x0F8000F0) == 0x00800090) return armMull(op);
            if ((op & 0x0FB00FF0) == 0x01000090) return armSwap(op);
            if (op & 0x60) return armHalfTransfer(op);
            return;
        }
        if ((op & 0x0FBF0FFF) == 0x010F0000) return armMrs(op);
        if ((op & 0x0DB0F000) == 0x0120F000) return armMsr(op);
        return armDataProc(op);
    case 1:
        if ((op & 0x0FB0F000) == 0x0320F000) return armMsr(op);
        return armDataProc(op);
    case 2:
        return armSingleTransfer(op);
    case 3:
        if (op & 0x10) return; // undefined instruction
        return armSingleTransfer(op);
    case 4:
        return armBlockTransfer(op);
    case 5: {
        s32 off = s32(op << 8) >> 6;
        if (op & (1u << 24)) r[14] = r[15] - 4;
        branchTo(r[15] + off);
        return;
    }
    case 6:
        return; // coprocessor transfers: not present on this system
    default:
        if (op & (1u << 24)) swi((op >> 16) & 0xFF);
        return;
    }
}

void Cpu::armDataProc(u32 op) {
    int opc = (op >> 21) & 15;
    bool s = (op >> 20) & 1;
    int rn = (op >> 16) & 15, rd = (op >> 12) & 15;
    bool c = flagC();
    u32 a, op2;
    if (op & (1u << 25)) {
        int rot = ((op >> 8) & 15) * 2;
        op2 = ror32(op & 0xFF, rot);
        if (rot) c = op2 >> 31;
        a = r[rn];
    } else {
        int rm = op & 15, type = (op >> 5) & 3;
        if (op & 0x10) {
            u32 amt = r[(op >> 8) & 15] & 0xFF;
            u32 vm = r[rm] + (rm == 15 ? 4 : 0);
            a = r[rn] + (rn == 15 ? 4 : 0);
            cycles++;
            op2 = shiftReg(type, vm, amt, c);
        } else {
            op2 = shiftImm(type, r[rm], (op >> 7) & 31, c);
            a = r[rn];
        }
    }

    bool flags = s && rd != 15;
    bool logical = false;
    u32 res = 0;
    switch (opc) {
    case 0x0: res = a & op2; logical = true; break;           // AND
    case 0x1: res = a ^ op2; logical = true; break;           // EOR
    case 0x2: res = sub(a, op2, flags); break;                // SUB
    case 0x3: res = sub(op2, a, flags); break;                // RSB
    case 0x4: res = add(a, op2, flags); break;                // ADD
    case 0x5: res = adc(a, op2, flags); break;                // ADC
    case 0x6: res = sbc(a, op2, flags); break;                // SBC
    case 0x7: res = sbc(op2, a, flags); break;                // RSC
    case 0x8: res = a & op2; logical = true; flags = true; break; // TST
    case 0x9: res = a ^ op2; logical = true; flags = true; break; // TEQ
    case 0xA: sub(a, op2, true); break;                       // CMP
    case 0xB: add(a, op2, true); break;                       // CMN
    case 0xC: res = a | op2; logical = true; break;           // ORR
    case 0xD: res = op2; logical = true; break;               // MOV
    case 0xE: res = a & ~op2; logical = true; break;          // BIC
    case 0xF: res = ~op2; logical = true; break;              // MVN
    }
    if (logical && flags) {
        setNZ(res);
        setC(c);
    }
    if (opc >= 0x8 && opc <= 0xB) {
        // TST/TEQ/CMP/CMN with Rd = PC ("xxxP") copy SPSR to CPSR without branching.
        if (rd == 15 && bankIndex(cpsr & 0x1F) != 0) setCpsr(spsr());
        return;
    }
    if (rd == 15) {
        if (s && bankIndex(cpsr & 0x1F) != 0) setCpsr(spsr());
        branchTo(res);
    } else {
        r[rd] = res;
    }
}

void Cpu::armMul(u32 op) {
    int rd = (op >> 16) & 15, rn = (op >> 12) & 15, rs = (op >> 8) & 15, rm = op & 15;
    u32 res = r[rm] * r[rs];
    mulCycles(r[rs], true);
    if (op & (1u << 21)) {
        res += r[rn];
        cycles++;
    }
    if (op & (1u << 20)) setNZ(res);
    r[rd] = res;
}

void Cpu::armMull(u32 op) {
    int hi = (op >> 16) & 15, lo = (op >> 12) & 15, rs = (op >> 8) & 15, rm = op & 15;
    bool isSigned = op & (1u << 22);
    u64 res = isSigned ? u64(s64(s32(r[rm])) * s64(s32(r[rs]))) : u64(r[rm]) * r[rs];
    mulCycles(r[rs], isSigned);
    cycles++;
    if (op & (1u << 21)) {
        res += (u64(r[hi]) << 32) | r[lo];
        cycles++;
    }
    if (op & (1u << 20)) {
        cpsr = (cpsr & ~(FLAG_N | FLAG_Z)) | (u32(res >> 32) & FLAG_N) | (res ? 0 : FLAG_Z);
    }
    r[lo] = u32(res);
    r[hi] = u32(res >> 32);
}

void Cpu::armSwap(u32 op) {
    int rn = (op >> 16) & 15, rd = (op >> 12) & 15, rm = op & 15;
    u32 addr = r[rn];
    u32 src = r[rm];
    u32 tmp;
    if (op & (1u << 22)) {
        tmp = read8(addr);
        write8(addr, src);
    } else {
        tmp = ror32(read32(addr), (addr & 3) * 8);
        write32(addr, src);
    }
    cycles++;
    r[rd] = tmp;
}

void Cpu::armHalfTransfer(u32 op) {
    bool p = op & (1u << 24), u = op & (1u << 23), w = op & (1u << 21), l = op & (1u << 20);
    int rn = (op >> 16) & 15, rd = (op >> 12) & 15, sh = (op >> 5) & 3;
    u32 off = (op & (1u << 22)) ? (((op >> 4) & 0xF0) | (op & 0xF)) : r[op & 15];
    u32 base = r[rn];
    u32 moved = u ? base + off : base - off;
    u32 addr = p ? moved : base;
    if (l) {
        u32 v;
        if (sh == 1) {
            v = read16(addr);
            if (addr & 1) v = ror32(v, 8);
        } else if (sh == 2) {
            v = u32(s32(s8(read8(addr))));
        } else {
            v = (addr & 1) ? u32(s32(s8(read8(addr)))) : u32(s32(s16(read16(addr))));
        }
        cycles++;
        if (!p || w) writeReg(rn, moved);
        writeReg(rd, v);
    } else {
        if (sh == 1) write16(addr, r[rd] + (rd == 15 ? 4 : 0));
        if (!p || w) writeReg(rn, moved);
    }
}

void Cpu::armSingleTransfer(u32 op) {
    bool p = op & (1u << 24), u = op & (1u << 23), b = op & (1u << 22), w = op & (1u << 21),
         l = op & (1u << 20);
    int rn = (op >> 16) & 15, rd = (op >> 12) & 15;
    u32 off;
    if (op & (1u << 25)) {
        bool c = flagC();
        off = shiftImm((op >> 5) & 3, r[op & 15], (op >> 7) & 31, c);
    } else {
        off = op & 0xFFF;
    }
    u32 base = r[rn];
    u32 moved = u ? base + off : base - off;
    u32 addr = p ? moved : base;
    if (l) {
        u32 v = b ? read8(addr) : ror32(read32(addr), (addr & 3) * 8);
        cycles++;
        if (!p || w) writeReg(rn, moved);
        writeReg(rd, v);
    } else {
        u32 v = r[rd] + (rd == 15 ? 4 : 0);
        if (b) write8(addr, v);
        else write32(addr, v);
        if (!p || w) writeReg(rn, moved);
    }
}

void Cpu::armBlockTransfer(u32 op) {
    bool p = op & (1u << 24), u = op & (1u << 23), s = op & (1u << 22), w = op & (1u << 21),
         l = op & (1u << 20);
    int rn = (op >> 16) & 15;
    u32 rlist = op & 0xFFFF;
    u32 base = r[rn];
    int n = popcount16(rlist);
    bool empty = rlist == 0;
    if (empty) { rlist = 0x8000; n = 16; }

    u32 start, newBase;
    if (u) {
        start = p ? base + 4 : base;
        newBase = base + 4 * n;
    } else {
        start = p ? base - 4 * n : base - 4 * n + 4;
        newBase = base - 4 * n;
    }
    if (empty) n = 1;

    bool userBank = s && !(l && (rlist & 0x8000));
    u32 oldMode = cpsr & 0x1F;
    if (userBank) switchMode(USR);

    u32 addr = start;
    bool seq = false;
    if (l) {
        if (w) r[rn] = newBase;
        u32 pcVal = 0;
        for (int i = 0; i < 16; i++) {
            if (!(rlist & (1u << i))) continue;
            u32 v = read32(addr, seq);
            seq = true;
            if (i == 15) pcVal = v;
            else r[i] = v;
            addr += 4;
        }
        cycles++;
        if (userBank) switchMode(oldMode);
        if (rlist & 0x8000) {
            if (s && bankIndex(cpsr & 0x1F) != 0) setCpsr(spsr());
            branchTo(pcVal);
        }
    } else {
        bool first = true;
        for (int i = 0; i < 16; i++) {
            if (!(rlist & (1u << i))) continue;
            u32 v = r[i];
            if (i == 15) v += 4;
            if (i == rn && !first && w) v = newBase;
            write32(addr, v, seq);
            seq = true;
            first = false;
            addr += 4;
        }
        if (userBank) switchMode(oldMode);
        if (w) r[rn] = newBase;
    }
}

void Cpu::armMrs(u32 op) {
    int rd = (op >> 12) & 15;
    r[rd] = (op & (1u << 22)) ? spsr() : cpsr;
}

void Cpu::armMsr(u32 op) {
    u32 v = (op & (1u << 25)) ? ror32(op & 0xFF, ((op >> 8) & 15) * 2) : r[op & 15];
    u32 mask = 0;
    if (op & (1u << 19)) mask |= 0xFF000000;
    if (op & (1u << 18)) mask |= 0x00FF0000;
    if (op & (1u << 17)) mask |= 0x0000FF00;
    if (op & (1u << 16)) mask |= 0x000000FF;
    if (op & (1u << 22)) {
        if (bankIndex(cpsr & 0x1F) != 0) spsr() = (spsr() & ~mask) | (v & mask);
        return;
    }
    if ((cpsr & 0x1F) == USR) mask &= 0xFF000000;
    u32 nv = (cpsr & ~mask) | (v & mask);
    nv = (nv & ~FLAG_T) | (cpsr & FLAG_T) | 0x10;
    setCpsr(nv);
}

// ---------------------------------------------------------------- Thumb
void Cpu::execThumb(u16 op) {
    switch (op >> 13) {
    case 0: {
        int rd = op & 7, rs = (op >> 3) & 7;
        if (((op >> 11) & 3) == 3) { // add/sub register or 3-bit immediate
            u32 v = (op & (1u << 10)) ? u32((op >> 6) & 7) : r[(op >> 6) & 7];
            r[rd] = (op & (1u << 9)) ? sub(r[rs], v, true) : add(r[rs], v, true);
        } else { // move shifted register
            bool c = flagC();
            u32 res = shiftImm((op >> 11) & 3, r[rs], (op >> 6) & 31, c);
            setNZ(res);
            setC(c);
            r[rd] = res;
        }
        return;
    }
    case 1: { // mov/cmp/add/sub immediate
        int rd = (op >> 8) & 7;
        u32 imm = op & 0xFF;
        switch ((op >> 11) & 3) {
        case 0: r[rd] = imm; setNZ(imm); break;
        case 1: sub(r[rd], imm, true); break;
        case 2: r[rd] = add(r[rd], imm, true); break;
        case 3: r[rd] = sub(r[rd], imm, true); break;
        }
        return;
    }
    case 2: {
        if ((op >> 10) == 0x10) return thumbAlu(op);
        if ((op >> 10) == 0x11) return thumbHiReg(op);
        if ((op >> 11) == 0x09) { // PC-relative load
            u32 addr = (r[15] & ~2u) + (op & 0xFF) * 4;
            r[(op >> 8) & 7] = read32(addr);
            cycles++;
            return;
        }
        int ro = (op >> 6) & 7, rb = (op >> 3) & 7, rd = op & 7;
        u32 addr = r[rb] + r[ro];
        switch ((op >> 9) & 7) {
        case 0: write32(addr, r[rd]); break;                       // STR
        case 1: write16(addr, r[rd]); break;                       // STRH
        case 2: write8(addr, r[rd]); break;                              // STRB
        case 3: r[rd] = u32(s32(s8(read8(addr)))); cycles++; break;      // LDSB
        case 4: r[rd] = ror32(read32(addr), (addr & 3) * 8); cycles++; break; // LDR
        case 5: {                                                        // LDRH
            u32 v = read16(addr);
            r[rd] = (addr & 1) ? ror32(v, 8) : v;
            cycles++;
            break;
        }
        case 6: r[rd] = read8(addr); cycles++; break;                    // LDRB
        case 7:                                                          // LDSH
            r[rd] = (addr & 1) ? u32(s32(s8(read8(addr)))) : u32(s32(s16(read16(addr))));
            cycles++;
            break;
        }
        return;
    }
    case 3: { // load/store with 5-bit immediate offset
        int rb = (op >> 3) & 7, rd = op & 7;
        u32 off = (op >> 6) & 31;
        bool load = op & (1u << 11);
        if (op & (1u << 12)) {
            u32 addr = r[rb] + off;
            if (load) { r[rd] = read8(addr); cycles++; }
            else write8(addr, r[rd]);
        } else {
            u32 addr = r[rb] + off * 4;
            if (load) { r[rd] = ror32(read32(addr), (addr & 3) * 8); cycles++; }
            else write32(addr, r[rd]);
        }
        return;
    }
    case 4: {
        bool load = op & (1u << 11);
        if (!(op & (1u << 12))) { // halfword immediate
            int rb = (op >> 3) & 7, rd = op & 7;
            u32 addr = r[rb] + ((op >> 6) & 31) * 2;
            if (load) {
                u32 v = read16(addr);
                r[rd] = (addr & 1) ? ror32(v, 8) : v;
                cycles++;
            } else {
                write16(addr, r[rd]);
            }
        } else { // SP-relative
            int rd = (op >> 8) & 7;
            u32 addr = r[13] + (op & 0xFF) * 4;
            if (load) { r[rd] = ror32(read32(addr), (addr & 3) * 8); cycles++; }
            else write32(addr, r[rd]);
        }
        return;
    }
    case 5: {
        if (!(op & (1u << 12))) { // load address
            u32 imm = (op & 0xFF) * 4;
            r[(op >> 8) & 7] = (op & (1u << 11)) ? r[13] + imm : (r[15] & ~2u) + imm;
            return;
        }
        if ((op & 0x0F00) == 0x0000) { // add offset to SP
            u32 imm = (op & 0x7F) * 4;
            r[13] = (op & 0x80) ? r[13] - imm : r[13] + imm;
            return;
        }
        if ((op & 0x0600) == 0x0400) { // push / pop
            bool load = op & (1u << 11), extra = op & (1u << 8);
            u32 rlist = op & 0xFF;
            if (rlist == 0 && !extra) {
                if (load) {
                    u32 v = read32(r[13] & ~3u);
                    r[13] += 0x40;
                    branchTo(v);
                } else {
                    r[13] -= 0x40;
                    write32(r[13] & ~3u, r[15] + 2);
                }
                return;
            }
            bool seq = false;
            if (load) {
                u32 addr = r[13];
                for (int i = 0; i < 8; i++) {
                    if (!(rlist & (1u << i))) continue;
                    r[i] = read32(addr, seq);
                    seq = true;
                    addr += 4;
                }
                u32 pcVal = 0;
                if (extra) { pcVal = read32(addr, seq); addr += 4; }
                r[13] = addr;
                cycles++;
                if (extra) branchTo(pcVal);
            } else {
                int n = popcount16(rlist) + (extra ? 1 : 0);
                u32 addr = r[13] - 4 * n;
                r[13] = addr;
                for (int i = 0; i < 8; i++) {
                    if (!(rlist & (1u << i))) continue;
                    write32(addr, r[i], seq);
                    seq = true;
                    addr += 4;
                }
                if (extra) write32(addr, r[14], seq);
            }
            return;
        }
        return;
    }
    case 6: {
        if (!(op & (1u << 12))) { // LDMIA / STMIA
            thumbBlock(r[(op >> 8) & 7], op & 0xFF, op & (1u << 11), true, (op >> 8) & 7);
            return;
        }
        u32 cond = (op >> 8) & 15;
        if (cond == 15) { swi(op & 0xFF); return; }
        if (cond == 14) return;
        if (condPassed(cond)) branchTo(r[15] + u32(s32(s8(op & 0xFF)) * 2));
        return;
    }
    default: {
        switch ((op >> 11) & 3) {
        case 0: branchTo(r[15] + u32(s32(u32(op) << 21) >> 20)); return;
        case 1: return;
        case 2: r[14] = r[15] + u32(s32(u32(op) << 21) >> 9); return;
        default: {
            u32 next = r[15] - 2;
            u32 target = r[14] + ((op & 0x7FF) << 1);
            r[14] = next | 1;
            branchTo(target);
            return;
        }
        }
    }
    }
}

void Cpu::thumbBlock(u32 base, u32 rlist, bool load, bool, int rb) {
    u32 addr = base;
    if (rlist == 0) {
        if (load) {
            u32 v = read32(addr);
            r[rb] = addr + 0x40;
            branchTo(v);
        } else {
            write32(addr, r[15] + 2);
            r[rb] = addr + 0x40;
        }
        return;
    }
    bool seq = false;
    if (load) {
        for (int i = 0; i < 8; i++) {
            if (!(rlist & (1u << i))) continue;
            r[i] = read32(addr, seq);
            seq = true;
            addr += 4;
        }
        if (!(rlist & (1u << rb))) r[rb] = addr;
        cycles++;
    } else {
        u32 newBase = addr + 4 * popcount16(rlist);
        bool first = true;
        for (int i = 0; i < 8; i++) {
            if (!(rlist & (1u << i))) continue;
            u32 v = (i == rb && !first) ? newBase : r[i];
            write32(addr, v, seq);
            seq = true;
            first = false;
            addr += 4;
        }
        r[rb] = newBase;
    }
}

void Cpu::thumbAlu(u16 op) {
    int rd = op & 7, rs = (op >> 3) & 7;
    u32 a = r[rd], b = r[rs];
    bool c = flagC();
    switch ((op >> 6) & 15) {
    case 0x0: r[rd] = a & b; setNZ(r[rd]); break;                         // AND
    case 0x1: r[rd] = a ^ b; setNZ(r[rd]); break;                         // EOR
    case 0x2: case 0x3: case 0x4: case 0x7: {                             // LSL LSR ASR ROR
        static const int types[8] = {0, 0, 0, 1, 2, 0, 0, 3};
        u32 res = shiftReg(types[(op >> 6) & 7], a, b & 0xFF, c);
        setNZ(res);
        setC(c);
        r[rd] = res;
        cycles++;
        break;
    }
    case 0x5: r[rd] = adc(a, b, true); break;                             // ADC
    case 0x6: r[rd] = sbc(a, b, true); break;                             // SBC
    case 0x8: setNZ(a & b); break;                                        // TST
    case 0x9: r[rd] = sub(0, b, true); break;                             // NEG
    case 0xA: sub(a, b, true); break;                                     // CMP
    case 0xB: add(a, b, true); break;                                     // CMN
    case 0xC: r[rd] = a | b; setNZ(r[rd]); break;                         // ORR
    case 0xD: r[rd] = a * b; mulCycles(a, true); setNZ(r[rd]); break;     // MUL
    case 0xE: r[rd] = a & ~b; setNZ(r[rd]); break;                        // BIC
    case 0xF: r[rd] = ~b; setNZ(r[rd]); break;                            // MVN
    }
}

void Cpu::thumbHiReg(u16 op) {
    int rd = (op & 7) | ((op >> 4) & 8);
    int rs = (op >> 3) & 15;
    u32 v = r[rs];
    switch ((op >> 8) & 3) {
    case 0: writeReg(rd, r[rd] + v); break;
    case 1: sub(r[rd], v, true); break;
    case 2: writeReg(rd, v); break;
    case 3:
        if (v & 1) cpsr |= FLAG_T;
        else cpsr &= ~FLAG_T;
        branchTo(v);
        break;
    }
}

} // namespace mulator
