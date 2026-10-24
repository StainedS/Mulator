#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

namespace mulator {

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using s8 = int8_t;
using s16 = int16_t;
using s32 = int32_t;
using s64 = int64_t;

constexpr u32 CPU_CLOCK = 16777216;
constexpr int CYCLES_HDRAW = 1006;
constexpr int CYCLES_HBLANK = 226;
constexpr int CYCLES_PER_LINE = CYCLES_HDRAW + CYCLES_HBLANK;
constexpr int VISIBLE_LINES = 160;
constexpr int TOTAL_LINES = 228;

enum IrqBit : u16 {
    IRQ_VBLANK = 1 << 0,
    IRQ_HBLANK = 1 << 1,
    IRQ_VCOUNT = 1 << 2,
    IRQ_TIMER0 = 1 << 3,
    IRQ_DMA0 = 1 << 8,
    IRQ_KEYPAD = 1 << 12,
    IRQ_GAMEPAK = 1 << 13,
};

inline u32 ror32(u32 v, unsigned s) {
    s &= 31;
    return s ? (v >> s) | (v << (32 - s)) : v;
}

inline u16 rd16(const u8* p) { return u16(p[0] | (p[1] << 8)); }
inline u32 rd32(const u8* p) { return u32(p[0] | (p[1] << 8) | (p[2] << 16) | (u32(p[3]) << 24)); }
inline void wr16(u8* p, u16 v) { p[0] = u8(v); p[1] = u8(v >> 8); }
inline void wr32(u8* p, u32 v) { p[0] = u8(v); p[1] = u8(v >> 8); p[2] = u8(v >> 16); p[3] = u8(v >> 24); }

// Save-state (de)serialization. Components implement
//   template <class S> void serialize(S& s)
// and call s.pod(field) / s.vec(vector) symmetrically for both directions.
struct StateWriter {
    std::vector<u8> buf;
    template <class T> void pod(T& v) {
        static_assert(std::is_trivially_copyable<T>::value, "pod only");
        const u8* p = reinterpret_cast<const u8*>(&v);
        buf.insert(buf.end(), p, p + sizeof(T));
    }
    void vec(std::vector<u8>& v) {
        u32 n = u32(v.size());
        pod(n);
        buf.insert(buf.end(), v.begin(), v.end());
    }
};

struct StateReader {
    const u8* data;
    size_t size;
    size_t pos = 0;
    bool ok = true;
    StateReader(const u8* d, size_t n) : data(d), size(n) {}
    template <class T> void pod(T& v) {
        static_assert(std::is_trivially_copyable<T>::value, "pod only");
        if (!ok || pos + sizeof(T) > size) { ok = false; return; }
        std::memcpy(&v, data + pos, sizeof(T));
        pos += sizeof(T);
    }
    void vec(std::vector<u8>& v) {
        u32 n = 0;
        pod(n);
        if (!ok || pos + n > size) { ok = false; return; }
        v.assign(data + pos, data + pos + n);
        pos += n;
    }
};

} // namespace mulator
