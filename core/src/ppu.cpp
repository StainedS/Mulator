// Scanline renderer: tiled/affine/bitmap backgrounds, sprites, windows, blending.
#include "system.h"

namespace mulator {

static constexpr u16 TRANSPARENT = 0x8000;

static const u32* colorLut() {
    static u32 lut[0x8000];
    static bool init = false;
    if (!init) {
        for (u32 c = 0; c < 0x8000; c++) {
            u32 r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
            r = (r << 3) | (r >> 2);
            g = (g << 3) | (g >> 2);
            b = (b << 3) | (b >> 2);
            lut[c] = 0xFF000000u | (b << 16) | (g << 8) | r;
        }
        init = true;
    }
    return lut;
}

static const u8 kObjSize[3][4][2] = {
    {{8, 8}, {16, 16}, {32, 32}, {64, 64}},
    {{16, 8}, {32, 8}, {32, 16}, {64, 32}},
    {{8, 16}, {8, 32}, {16, 32}, {32, 64}},
};

void Ppu::reset() {
    colorLut();
    for (auto& p : framebuffer) p = 0xFF000000u;
    refX[0] = refX[1] = refY[0] = refY[1] = 0;
}

void Ppu::affineRefWritten(int bg) {
    u32 base = bg == 0 ? reg::BG2X : reg::BG3X;
    refX[bg] = s32(rd32(sys.bus.io + base) << 4) >> 4;
    refY[bg] = s32(rd32(sys.bus.io + base + 4) << 4) >> 4;
}

void Ppu::vblankLatch() {
    affineRefWritten(0);
    affineRefWritten(1);
}

void Ppu::lineDone() {
    const u8* io = sys.bus.io;
    refX[0] += s16(rd16(io + reg::BG2PA + 2));
    refY[0] += s16(rd16(io + reg::BG2PA + 6));
    refX[1] += s16(rd16(io + reg::BG3PA + 2));
    refY[1] += s16(rd16(io + reg::BG3PA + 6));
}

void Ppu::renderLine(int line) {
    const u8* io = sys.bus.io;
    u16 dispcnt = rd16(io + reg::DISPCNT);
    u32* dst = framebuffer + line * 240;
    if (dispcnt & 0x80) { // forced blank
        for (int x = 0; x < 240; x++) dst[x] = 0xFFFFFFFFu;
        return;
    }
    for (int bg = 0; bg < 4; bg++)
        for (int x = 0; x < 240; x++) bgLine[bg][x] = TRANSPARENT;

    int mode = dispcnt & 7;
    switch (mode) {
    case 0:
        for (int bg = 0; bg < 4; bg++)
            if (dispcnt & (0x100 << bg)) renderTextBg(bg, line);
        break;
    case 1:
        if (dispcnt & 0x100) renderTextBg(0, line);
        if (dispcnt & 0x200) renderTextBg(1, line);
        if (dispcnt & 0x400) renderAffineBg(2);
        break;
    case 2:
        if (dispcnt & 0x400) renderAffineBg(2);
        if (dispcnt & 0x800) renderAffineBg(3);
        break;
    case 3: case 4: case 5:
        if (dispcnt & 0x400) renderBitmapBg(mode);
        break;
    default:
        break;
    }

    for (int x = 0; x < 240; x++) {
        objColor[x] = TRANSPARENT;
        objPrio[x] = 4;
        objSemi[x] = false;
        objWin[x] = false;
    }
    if (dispcnt & 0x1000) renderObjs(line);
    compose(line);
}

void Ppu::renderTextBg(int bg, int line) {
    const u8* io = sys.bus.io;
    const u8* vram = sys.bus.vram;
    const u8* pal = sys.bus.pal;
    u16 cnt = rd16(io + reg::BG0CNT + bg * 2);
    u32 charBase = ((cnt >> 2) & 3) * 0x4000u;
    u32 screenBase = ((cnt >> 8) & 31) * 0x800u;
    bool c256 = cnt & 0x80;
    int size = cnt >> 14;
    int w = (size & 1) ? 512 : 256, h = (size & 2) ? 512 : 256;
    int hofs = rd16(io + reg::BG0HOFS + bg * 4) & 0x1FF;
    int vofs = rd16(io + reg::BG0HOFS + bg * 4 + 2) & 0x1FF;

    u16 mosaic = rd16(io + reg::MOSAIC);
    bool mos = cnt & 0x40;
    int mh = (mosaic & 15) + 1, mv = ((mosaic >> 4) & 15) + 1;
    int srcLine = mos ? line - line % mv : line;

    int y = (srcLine + vofs) & (h - 1);
    int ty = y >> 3, fy = y & 7;
    u16* outLine = bgLine[bg];

    for (int x = 0; x < 240; x++) {
        int xm = mos ? x - x % mh : x;
        int sx = (xm + hofs) & (w - 1);
        int tx = sx >> 3;
        u32 block = u32((tx >> 5) + (ty >> 5) * (w >> 8));
        u32 mapAddr = screenBase + block * 0x800 + u32(((ty & 31) * 32 + (tx & 31)) * 2);
        u16 entry = rd16(vram + mapAddr);
        int tile = entry & 0x3FF;
        int px = sx & 7;
        if (entry & 0x400) px = 7 - px;
        int py = (entry & 0x800) ? 7 - fy : fy;
        u16 color = TRANSPARENT;
        if (c256) {
            u32 addr = charBase + u32(tile) * 64 + u32(py * 8 + px);
            if (addr < 0x10000) {
                u8 idx = vram[addr];
                if (idx) color = rd16(pal + idx * 2) & 0x7FFF;
            }
        } else {
            u32 addr = charBase + u32(tile) * 32 + u32(py * 4 + px / 2);
            if (addr < 0x10000) {
                u8 b = vram[addr];
                u8 idx = (px & 1) ? (b >> 4) : (b & 15);
                if (idx) color = rd16(pal + ((entry >> 12) * 16 + idx) * 2) & 0x7FFF;
            }
        }
        outLine[x] = color;
    }
}

void Ppu::renderAffineBg(int bg) {
    const u8* io = sys.bus.io;
    const u8* vram = sys.bus.vram;
    const u8* pal = sys.bus.pal;
    int a = bg - 2;
    u16 cnt = rd16(io + reg::BG0CNT + bg * 2);
    u32 charBase = ((cnt >> 2) & 3) * 0x4000u;
    u32 screenBase = ((cnt >> 8) & 31) * 0x800u;
    int size = 128 << (cnt >> 14);
    bool wrap = cnt & 0x2000;
    u32 pbase = a == 0 ? reg::BG2PA : reg::BG3PA;
    s32 pa = s16(rd16(io + pbase)), pc = s16(rd16(io + pbase + 4));
    s32 fx = refX[a], fy = refY[a];
    u16* outLine = bgLine[bg];

    for (int x = 0; x < 240; x++, fx += pa, fy += pc) {
        int tx = fx >> 8, ty = fy >> 8;
        if (wrap) {
            tx &= size - 1;
            ty &= size - 1;
        } else if (tx < 0 || ty < 0 || tx >= size || ty >= size) {
            continue;
        }
        u8 tile = vram[screenBase + u32((ty >> 3) * (size >> 3) + (tx >> 3))];
        u32 addr = charBase + u32(tile) * 64 + u32((ty & 7) * 8 + (tx & 7));
        if (addr >= 0x10000) continue;
        u8 idx = vram[addr];
        if (idx) outLine[x] = rd16(pal + idx * 2) & 0x7FFF;
    }
}

void Ppu::renderBitmapBg(int mode) {
    const u8* io = sys.bus.io;
    const u8* vram = sys.bus.vram;
    const u8* pal = sys.bus.pal;
    u16 dispcnt = rd16(io + reg::DISPCNT);
    u32 frame = (mode != 3 && (dispcnt & 0x10)) ? 0xA000 : 0;
    s32 pa = s16(rd16(io + reg::BG2PA)), pc = s16(rd16(io + reg::BG2PA + 4));
    s32 fx = refX[0], fy = refY[0];
    int w = mode == 5 ? 160 : 240, h = mode == 5 ? 128 : 160;
    u16* outLine = bgLine[2];

    for (int x = 0; x < 240; x++, fx += pa, fy += pc) {
        int tx = fx >> 8, ty = fy >> 8;
        if (tx < 0 || ty < 0 || tx >= w || ty >= h) continue;
        if (mode == 4) {
            u8 idx = vram[frame + u32(ty * 240 + tx)];
            if (idx) outLine[x] = rd16(pal + idx * 2) & 0x7FFF;
        } else {
            outLine[x] = rd16(vram + frame + u32((ty * w + tx) * 2)) & 0x7FFF;
        }
    }
}

void Ppu::renderObjs(int line) {
    const u8* io = sys.bus.io;
    const u8* vram = sys.bus.vram;
    const u8* pal = sys.bus.pal;
    const u8* oam = sys.bus.oam;
    u16 dispcnt = rd16(io + reg::DISPCNT);
    bool map1D = dispcnt & 0x40;
    bool bitmapMode = (dispcnt & 7) >= 3;
    u16 mosaic = rd16(io + reg::MOSAIC);
    int mh = ((mosaic >> 8) & 15) + 1, mv = ((mosaic >> 12) & 15) + 1;

    for (int i = 0; i < 128; i++) {
        u16 a0 = rd16(oam + i * 8), a1 = rd16(oam + i * 8 + 2), a2 = rd16(oam + i * 8 + 4);
        bool affine = a0 & 0x100;
        if (!affine && (a0 & 0x200)) continue;
        int shape = a0 >> 14;
        if (shape == 3) continue;
        int objMode = (a0 >> 10) & 3;
        if (objMode == 3) continue;
        int w = kObjSize[shape][a1 >> 14][0], h = kObjSize[shape][a1 >> 14][1];
        int bw = w, bh = h;
        if (affine && (a0 & 0x200)) { bw *= 2; bh *= 2; }

        int y = a0 & 0xFF;
        if (y + bh > 256) y -= 256;
        bool mos = a0 & 0x1000;
        int srcLine = mos ? line - line % mv : line;
        int dy = srcLine - y;
        if (line - y < 0 || line - y >= bh) continue;
        if (dy < 0) dy = 0;

        int x = a1 & 0x1FF;
        if (x >= 240) x -= 512;
        bool c256 = a0 & 0x2000;
        int tile = a2 & 0x3FF;
        int prio = (a2 >> 10) & 3;
        int palBank = a2 >> 12;
        if (bitmapMode && tile < 512) continue;

        s32 pa = 256, pb = 0, pc = 0, pd = 256;
        if (affine) {
            int g = (a1 >> 9) & 31;
            pa = s16(rd16(oam + g * 32 + 6));
            pb = s16(rd16(oam + g * 32 + 14));
            pc = s16(rd16(oam + g * 32 + 22));
            pd = s16(rd16(oam + g * 32 + 30));
        }
        bool hflip = !affine && (a1 & 0x1000);
        bool vflip = !affine && (a1 & 0x2000);
        int rowTiles = map1D ? (w / 8) * (c256 ? 2 : 1) : 32;

        for (int px = 0; px < bw; px++) {
            int sx = x + px;
            if (sx < 0 || sx >= 240) continue;
            int spx = mos ? (sx - sx % mh) - x : px;
            if (spx < 0) spx = 0;
            int tx, ty;
            if (affine) {
                int cx = spx - bw / 2, cy = dy - bh / 2;
                tx = ((pa * cx + pb * cy) >> 8) + w / 2;
                ty = ((pc * cx + pd * cy) >> 8) + h / 2;
                if (tx < 0 || ty < 0 || tx >= w || ty >= h) continue;
            } else {
                tx = hflip ? w - 1 - spx : spx;
                ty = vflip ? h - 1 - dy : dy;
            }
            int tileX = tx >> 3, tileY = ty >> 3;
            u16 color;
            if (c256) {
                int t = tile + tileY * rowTiles + tileX * 2;
                u32 addr = 0x10000 + u32(t & 0x3FF) * 32 + u32((ty & 7) * 8 + (tx & 7));
                u8 idx = vram[addr];
                if (!idx) continue;
                color = rd16(pal + 0x200 + idx * 2) & 0x7FFF;
            } else {
                int t = tile + tileY * rowTiles + tileX;
                u32 addr = 0x10000 + u32(t & 0x3FF) * 32 + u32((ty & 7) * 4 + (tx & 7) / 2);
                u8 b = vram[addr];
                u8 idx = (tx & 1) ? (b >> 4) : (b & 15);
                if (!idx) continue;
                color = rd16(pal + 0x200 + (palBank * 16 + idx) * 2) & 0x7FFF;
            }
            if (objMode == 2) {
                objWin[sx] = true;
                continue;
            }
            if (prio < objPrio[sx]) {
                objColor[sx] = color;
                objPrio[sx] = u8(prio);
                objSemi[sx] = objMode == 1;
            }
        }
    }
}

static inline bool inRange(int v, int lo, int hi) {
    if (lo <= hi) return v >= lo && v < hi;
    return v >= lo || v < hi; // wrapped window
}

static inline u16 blendAlpha(u16 a, u16 b, int eva, int evb) {
    int r = ((a & 31) * eva + (b & 31) * evb) >> 4;
    int g = (((a >> 5) & 31) * eva + ((b >> 5) & 31) * evb) >> 4;
    int bl = (((a >> 10) & 31) * eva + ((b >> 10) & 31) * evb) >> 4;
    if (r > 31) r = 31;
    if (g > 31) g = 31;
    if (bl > 31) bl = 31;
    return u16(r | (g << 5) | (bl << 10));
}

static inline u16 brighten(u16 c, int evy) {
    int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
    r += ((31 - r) * evy) >> 4;
    g += ((31 - g) * evy) >> 4;
    b += ((31 - b) * evy) >> 4;
    return u16(r | (g << 5) | (b << 10));
}

static inline u16 darken(u16 c, int evy) {
    int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
    r -= (r * evy) >> 4;
    g -= (g * evy) >> 4;
    b -= (b * evy) >> 4;
    return u16(r | (g << 5) | (b << 10));
}

void Ppu::compose(int line) {
    const u8* io = sys.bus.io;
    const u32* lut = colorLut();
    u16 dispcnt = rd16(io + reg::DISPCNT);
    u16 bldcnt = rd16(io + reg::BLDCNT);
    u16 bldalpha = rd16(io + reg::BLDALPHA);
    int blendMode = (bldcnt >> 6) & 3;
    int eva = bldalpha & 31, evb = (bldalpha >> 8) & 31, evy = rd16(io + reg::BLDY) & 31;
    if (eva > 16) eva = 16;
    if (evb > 16) evb = 16;
    if (evy > 16) evy = 16;

    static const u8 modeBgMask[8] = {0xF, 0x7, 0xC, 0x4, 0x4, 0x4, 0, 0};
    u8 bgMask = u8((dispcnt >> 8) & modeBgMask[dispcnt & 7]);

    // Backgrounds ordered by priority, then index.
    int order[4], nOrder = 0;
    int prio[4];
    for (int bg = 0; bg < 4; bg++) prio[bg] = rd16(io + reg::BG0CNT + bg * 2) & 3;
    for (int p = 0; p < 4; p++)
        for (int bg = 0; bg < 4; bg++)
            if ((bgMask & (1 << bg)) && prio[bg] == p) order[nOrder++] = bg;

    bool winAny = dispcnt & 0xE000;
    u16 winin = rd16(io + reg::WININ), winout = rd16(io + reg::WINOUT);
    auto winBounds = [&](u32 hReg, u32 vReg, int& x1, int& x2, bool& yIn) {
        u16 hv = rd16(io + hReg), vv = rd16(io + vReg);
        x1 = hv >> 8;
        x2 = hv & 0xFF;
        if (x2 > 240) x2 = 240;
        int y1 = vv >> 8, y2 = vv & 0xFF;
        if (y2 > 160) y2 = 160;
        yIn = inRange(line, y1, y2);
    };
    int w0x1 = 0, w0x2 = 0, w1x1 = 0, w1x2 = 0;
    bool w0y = false, w1y = false;
    if (dispcnt & 0x2000) winBounds(reg::WIN0H, reg::WIN0V, w0x1, w0x2, w0y);
    if (dispcnt & 0x4000) winBounds(reg::WIN1H, reg::WIN1V, w1x1, w1x2, w1y);

    u16 backdrop = rd16(sys.bus.pal) & 0x7FFF;
    u32* dst = framebuffer + line * 240;

    for (int x = 0; x < 240; x++) {
        u8 winMask = 0x3F;
        if (winAny) {
            if ((dispcnt & 0x2000) && w0y && inRange(x, w0x1, w0x2)) winMask = winin & 0x3F;
            else if ((dispcnt & 0x4000) && w1y && inRange(x, w1x1, w1x2)) winMask = (winin >> 8) & 0x3F;
            else if ((dispcnt & 0x8000) && objWin[x]) winMask = (winout >> 8) & 0x3F;
            else winMask = winout & 0x3F;
        }

        int layers[2] = {5, 5};
        u16 colors[2] = {backdrop, backdrop};
        int found = 0;
        bool objOn = (winMask & 0x10) && objPrio[x] < 4;
        for (int p = 0; p < 4 && found < 2; p++) {
            if (objOn && objPrio[x] == p) {
                layers[found] = 4;
                colors[found] = objColor[x];
                found++;
            }
            for (int i = 0; i < nOrder && found < 2; i++) {
                int bg = order[i];
                if (prio[bg] != p || !(winMask & (1 << bg))) continue;
                u16 c = bgLine[bg][x];
                if (c & TRANSPARENT) continue;
                layers[found] = bg;
                colors[found] = c;
                found++;
            }
        }

        u16 outColor = colors[0];
        bool secondTarget = bldcnt & (0x100 << layers[1]);
        if (layers[0] == 4 && objSemi[x] && secondTarget) {
            outColor = blendAlpha(colors[0], colors[1], eva, evb);
        } else if ((winMask & 0x20) && (bldcnt & (1 << layers[0]))) {
            switch (blendMode) {
            case 1: if (secondTarget) outColor = blendAlpha(colors[0], colors[1], eva, evb); break;
            case 2: outColor = brighten(colors[0], evy); break;
            case 3: outColor = darken(colors[0], evy); break;
            default: break;
            }
        }
        dst[x] = lut[outColor];
    }
}

} // namespace mulator
