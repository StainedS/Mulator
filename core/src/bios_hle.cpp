// High-level emulation of the system BIOS software interrupts, so no copyrighted
// BIOS dump is required to run games.
#include <cmath>
#include <vector>

#include "system.h"

namespace mulator {

static constexpr double kPi = 3.14159265358979323846;

static s32 hleArcTan(s32 i) {
    s32 a = -((i * i) >> 14);
    s32 b = ((0xA9 * a) >> 14) + 0x390;
    b = ((b * a) >> 14) + 0x91C;
    b = ((b * a) >> 14) + 0xFB6;
    b = ((b * a) >> 14) + 0x16AA;
    b = ((b * a) >> 14) + 0x2081;
    b = ((b * a) >> 14) + 0x3651;
    b = ((b * a) >> 14) + 0xA2F9;
    return (i * b) >> 16;
}

static s32 hleArcTan2(s32 x, s32 y) {
    if (!y) return x >= 0 ? 0 : 0x8000;
    if (!x) return y >= 0 ? 0x4000 : 0xC000;
    if (y >= 0) {
        if (x >= 0) {
            if (x >= y) return hleArcTan((y << 14) / x);
        } else if (-x >= y) {
            return hleArcTan((y << 14) / x) + 0x8000;
        }
        return 0x4000 - hleArcTan((x << 14) / y);
    }
    if (x <= 0) {
        if (-x > -y) return hleArcTan((y << 14) / x) + 0x8000;
    } else if (x >= -y) {
        return hleArcTan((y << 14) / x) + 0x10000;
    }
    return 0xC000 - hleArcTan((x << 14) / y);
}

bool System::hleSwi(u32 n) {
    u32* r = cpu.r;
    Bus& b = bus;

    auto writeOut = [&](u32 dst, const std::vector<u8>& data, bool vram) {
        if (vram) {
            for (size_t i = 0; i + 1 < data.size(); i += 2) b.write16(dst + u32(i), u16(data[i] | (data[i + 1] << 8)), true);
            if (data.size() & 1) b.write16(dst + u32(data.size() - 1), data.back(), true);
        } else {
            for (size_t i = 0; i < data.size(); i++) b.write8(dst + u32(i), data[i], true);
        }
    };

    switch (n) {
    case 0x00: { // SoftReset
        u8 flag = b.iwram[0x7FFA];
        std::memset(b.iwram + 0x7E00, 0, 0x200);
        cpu.reset(true);
        if (flag) cpu.branchTo(0x02000000);
        return true;
    }
    case 0x01: { // RegisterRamReset
        u32 f = r[0];
        if (f & 0x01) std::memset(b.ewram, 0, sizeof(b.ewram));
        if (f & 0x02) std::memset(b.iwram, 0, 0x7E00);
        if (f & 0x04) std::memset(b.pal, 0, sizeof(b.pal));
        if (f & 0x08) std::memset(b.vram, 0, sizeof(b.vram));
        if (f & 0x10) std::memset(b.oam, 0, sizeof(b.oam));
        if (f & 0x40) {
            for (u32 a = 0x60; a < 0xA8; a++) b.io[a] = 0;
            b.ioSet16(reg::SOUNDBIAS, 0x200);
        }
        if (f & 0x80) {
            for (u32 a = 0x000; a < 0x060; a++) b.io[a] = 0;
            b.ioSet16(reg::DISPCNT, 0x80);
            b.ioSet16(reg::BG2PA, 0x100);
            b.ioSet16(reg::BG2PA + 6, 0x100);
            b.ioSet16(reg::BG3PA, 0x100);
            b.ioSet16(reg::BG3PA + 6, 0x100);
        }
        return true;
    }
    case 0x02: // Halt
    case 0x03: // Stop
        cpu.halted = true;
        return true;
    case 0x05: // VBlankIntrWait
        r[0] = 1;
        r[1] = 1;
        // fallthrough
    case 0x04: { // IntrWait
        b.io[reg::IME] = 1;
        u16 flags = b.read16(0x03007FF8, false);
        if (r[0] && !cpu.intrWaitActive) flags &= u16(~r[1]);
        if (flags & r[1]) {
            b.write16(0x03007FF8, u16(flags & ~r[1]), false);
            cpu.intrWaitActive = false;
        } else {
            b.write16(0x03007FF8, flags, false);
            cpu.intrWaitActive = true;
            cpu.halted = true;
            cpu.branchTo(cpu.curAddr); // re-run this SWI after the interrupt returns
        }
        return true;
    }
    case 0x06: case 0x07: { // Div, DivArm
        s32 num = s32(n == 0x06 ? r[0] : r[1]);
        s32 den = s32(n == 0x06 ? r[1] : r[0]);
        if (den == 0) {
            r[0] = num < 0 ? u32(-1) : 1;
            r[1] = u32(num);
            r[3] = 1;
        } else if (num == INT32_MIN && den == -1) {
            r[0] = u32(INT32_MIN);
            r[1] = 0;
            r[3] = u32(INT32_MIN);
        } else {
            s32 q = num / den;
            r[0] = u32(q);
            r[1] = u32(num % den);
            r[3] = u32(q < 0 ? -q : q);
        }
        cpu.halted = false;
        b.cycles += 40;
        return true;
    }
    case 0x08: { // Sqrt
        u32 v = r[0], res = 0, bit = 1u << 30;
        while (bit > v) bit >>= 2;
        while (bit) {
            if (v >= res + bit) { v -= res + bit; res = (res >> 1) + bit; }
            else res >>= 1;
            bit >>= 2;
        }
        r[0] = res;
        b.cycles += 30;
        return true;
    }
    case 0x09: r[0] = u32(hleArcTan(s32(r[0]))); return true;
    case 0x0A: r[0] = u32(hleArcTan2(s32(r[0]), s32(r[1])) & 0xFFFF); return true;
    case 0x0B: { // CpuSet
        u32 src = r[0], dst = r[1], ctrl = r[2];
        if ((src >> 24) == 0) return true;
        u32 count = ctrl & 0x1FFFFF;
        bool fill = ctrl & (1u << 24);
        if (ctrl & (1u << 26)) {
            src &= ~3u; dst &= ~3u;
            u32 v = b.read32(src, false);
            for (u32 i = 0; i < count; i++) {
                if (!fill) v = b.read32(src + i * 4, true);
                b.write32(dst + i * 4, v, true);
            }
        } else {
            src &= ~1u; dst &= ~1u;
            u16 v = b.read16(src, false);
            for (u32 i = 0; i < count; i++) {
                if (!fill) v = b.read16(src + i * 2, true);
                b.write16(dst + i * 2, v, true);
            }
        }
        return true;
    }
    case 0x0C: { // CpuFastSet
        u32 src = r[0] & ~3u, dst = r[1] & ~3u, ctrl = r[2];
        if ((src >> 24) == 0) return true;
        u32 count = ((ctrl & 0x1FFFFF) + 7) & ~7u;
        bool fill = ctrl & (1u << 24);
        u32 v = b.read32(src, false);
        for (u32 i = 0; i < count; i++) {
            if (!fill) v = b.read32(src + i * 4, true);
            b.write32(dst + i * 4, v, true);
        }
        return true;
    }
    case 0x0D: r[0] = 0xBAAE187F; return true; // GetBiosChecksum
    case 0x0E: { // BgAffineSet
        u32 src = r[0], dst = r[1];
        for (u32 i = 0; i < r[2]; i++, src += 20, dst += 16) {
            double ox = s32(b.read32(src, false)) / 256.0;
            double oy = s32(b.read32(src + 4, false)) / 256.0;
            double cx = s16(b.read16(src + 8, false));
            double cy = s16(b.read16(src + 10, false));
            double sx = s16(b.read16(src + 12, false)) / 256.0;
            double sy = s16(b.read16(src + 14, false)) / 256.0;
            double theta = (b.read16(src + 16, false) >> 8) / 128.0 * kPi;
            double pa = std::cos(theta) * sx, pb = -std::sin(theta) * sx;
            double pc = std::sin(theta) * sy, pd = std::cos(theta) * sy;
            double rx = ox - (pa * cx + pb * cy);
            double ry = oy - (pc * cx + pd * cy);
            b.write16(dst, u16(s16(pa * 256)), false);
            b.write16(dst + 2, u16(s16(pb * 256)), false);
            b.write16(dst + 4, u16(s16(pc * 256)), false);
            b.write16(dst + 6, u16(s16(pd * 256)), false);
            b.write32(dst + 8, u32(s32(rx * 256)), false);
            b.write32(dst + 12, u32(s32(ry * 256)), false);
        }
        return true;
    }
    case 0x0F: { // ObjAffineSet
        u32 src = r[0], dst = r[1], stride = r[3];
        for (u32 i = 0; i < r[2]; i++, src += 8) {
            double sx = s16(b.read16(src, false)) / 256.0;
            double sy = s16(b.read16(src + 2, false)) / 256.0;
            double theta = (b.read16(src + 4, false) >> 8) / 128.0 * kPi;
            double pa = std::cos(theta) * sx, pb = -std::sin(theta) * sx;
            double pc = std::sin(theta) * sy, pd = std::cos(theta) * sy;
            b.write16(dst, u16(s16(pa * 256)), false);
            b.write16(dst + stride, u16(s16(pb * 256)), false);
            b.write16(dst + stride * 2, u16(s16(pc * 256)), false);
            b.write16(dst + stride * 3, u16(s16(pd * 256)), false);
            dst += stride * 4;
        }
        return true;
    }
    case 0x10: { // BitUnPack
        u32 src = r[0], dst = r[1], info = r[2];
        u32 len = b.read16(info, false);
        u32 srcW = b.read8(info + 2, false), dstW = b.read8(info + 3, false);
        u32 off = b.read32(info + 4, false);
        bool zeroFlag = off >> 31;
        off &= 0x7FFFFFFF;
        if (!srcW || srcW > 8 || !dstW || dstW > 32) return true;
        u32 dstMask = dstW == 32 ? 0xFFFFFFFFu : (1u << dstW) - 1;
        u32 outWord = 0, outBits = 0;
        for (u32 i = 0; i < len; i++) {
            u8 byte = b.read8(src + i, true);
            for (u32 bit = 0; bit < 8; bit += srcW) {
                u32 v = (byte >> bit) & ((1u << srcW) - 1);
                if (v || zeroFlag) v += off;
                outWord |= (v & dstMask) << outBits;
                outBits += dstW;
                if (outBits >= 32) {
                    b.write32(dst, outWord, true);
                    dst += 4;
                    outWord = 0;
                    outBits = 0;
                }
            }
        }
        return true;
    }
    case 0x11: case 0x12: { // LZ77UnComp (WRAM / VRAM)
        u32 src = r[0];
        u32 size = b.read32(src, false) >> 8;
        src += 4;
        std::vector<u8> out;
        out.reserve(size);
        while (out.size() < size) {
            u8 flags = b.read8(src++, true);
            for (int i = 0; i < 8 && out.size() < size; i++, flags <<= 1) {
                if (flags & 0x80) {
                    u8 b1 = b.read8(src++, true), b2 = b.read8(src++, true);
                    u32 len = (b1 >> 4) + 3u;
                    u32 disp = (((b1 & 15u) << 8) | b2) + 1;
                    for (u32 j = 0; j < len && out.size() < size; j++)
                        out.push_back(disp <= out.size() ? out[out.size() - disp] : 0);
                } else {
                    out.push_back(b.read8(src++, true));
                }
            }
        }
        writeOut(r[1], out, n == 0x12);
        return true;
    }
    case 0x13: { // HuffUnComp
        u32 src = r[0] & ~3u, dst = r[1];
        u32 header = b.read32(src, false);
        u32 bits = header & 15, size = header >> 8;
        if (bits != 4 && bits != 8) bits = 8;
        u32 treeSize = b.read8(src + 4, false);
        u32 treeBase = src + 5;
        u32 dataPtr = src + 4 + (treeSize + 1) * 2;
        u32 nodeAddr = treeBase;
        u8 node = b.read8(treeBase, false);
        u32 outWord = 0, outBits = 0, produced = 0;
        while (produced < size) {
            u32 stream = b.read32(dataPtr, true);
            dataPtr += 4;
            for (int bit = 31; bit >= 0 && produced < size; bit--) {
                int dir = (stream >> bit) & 1;
                u32 child = (nodeAddr & ~1u) + (node & 0x3F) * 2 + 2 + u32(dir);
                bool leaf = dir ? (node & 0x40) : (node & 0x80);
                u8 value = b.read8(child, true);
                if (leaf) {
                    outWord |= u32(value) << outBits;
                    outBits += bits;
                    if (outBits >= 32) {
                        b.write32(dst, outWord, true);
                        dst += 4;
                        produced += 4;
                        outWord = 0;
                        outBits = 0;
                    }
                    nodeAddr = treeBase;
                    node = b.read8(treeBase, true);
                } else {
                    nodeAddr = child;
                    node = value;
                }
            }
        }
        return true;
    }
    case 0x14: case 0x15: { // RLUnComp (WRAM / VRAM)
        u32 src = r[0];
        u32 size = b.read32(src, false) >> 8;
        src += 4;
        std::vector<u8> out;
        out.reserve(size);
        while (out.size() < size) {
            u8 flag = b.read8(src++, true);
            if (flag & 0x80) {
                u32 len = (flag & 0x7Fu) + 3;
                u8 v = b.read8(src++, true);
                for (u32 j = 0; j < len && out.size() < size; j++) out.push_back(v);
            } else {
                u32 len = (flag & 0x7Fu) + 1;
                for (u32 j = 0; j < len && out.size() < size; j++) out.push_back(b.read8(src++, true));
            }
        }
        writeOut(r[1], out, n == 0x15);
        return true;
    }
    case 0x16: case 0x17: { // Diff8bitUnFilter (WRAM / VRAM)
        u32 src = r[0];
        u32 size = b.read32(src, false) >> 8;
        src += 4;
        std::vector<u8> out;
        u8 acc = 0;
        for (u32 i = 0; i < size; i++) {
            acc = u8(acc + b.read8(src + i, true));
            out.push_back(acc);
        }
        writeOut(r[1], out, n == 0x17);
        return true;
    }
    case 0x18: { // Diff16bitUnFilter
        u32 src = r[0], dst = r[1];
        u32 size = b.read32(src, false) >> 8;
        src += 4;
        u16 acc = 0;
        for (u32 i = 0; i + 1 < size; i += 2) {
            acc = u16(acc + b.read16(src + i, true));
            b.write16(dst + i, acc, true);
        }
        return true;
    }
    case 0x19: { // SoundBias
        u16 bias = b.ioRead16(reg::SOUNDBIAS);
        b.ioSet16(reg::SOUNDBIAS, u16((bias & 0xFC00) | (r[0] ? 0x200 : 0)));
        return true;
    }
    case 0x1F: { // MidiKey2Freq
        u32 freq = b.read32(r[0] + 4, false);
        double exp = (180.0 - double(r[1]) - double(r[2]) / 256.0) / 12.0;
        r[0] = u32(freq / std::pow(2.0, exp));
        return true;
    }
    default:
        return true; // unimplemented calls (sound driver, multiboot) are no-ops
    }
}

} // namespace mulator
