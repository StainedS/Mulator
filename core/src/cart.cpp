// Cartridge ROM and battery-backed save memory (SRAM, Flash 64K/128K, EEPROM).
#include <algorithm>
#include <ctime>

#include "system.h"

namespace mulator {

void Cart::load(const u8* data, size_t size) {
    rom.assign(data, data + size);
    detectSaveType();
    resetState();
    dirty = false;
}

static bool romContains(const std::vector<u8>& rom, const char* needle) {
    size_t n = std::strlen(needle);
    if (rom.size() < n) return false;
    for (size_t i = 0; i + n <= rom.size(); i += 4) {
        if (rom[i] == u8(needle[0]) && std::memcmp(&rom[i], needle, n) == 0) return true;
    }
    return false;
}

void Cart::detectSaveType() {
    saveType = SaveType::None;
    if (romContains(rom, "EEPROM_V")) saveType = SaveType::Eeprom;
    else if (romContains(rom, "SRAM_V") || romContains(rom, "SRAM_F_V")) saveType = SaveType::Sram;
    else if (romContains(rom, "FLASH1M_V")) saveType = SaveType::Flash128;
    else if (romContains(rom, "FLASH_V") || romContains(rom, "FLASH512_V")) saveType = SaveType::Flash64;

    switch (saveType) {
    case SaveType::Sram: save.assign(0x8000, 0xFF); break;
    case SaveType::Flash64: save.assign(0x10000, 0xFF); break;
    case SaveType::Flash128: save.assign(0x20000, 0xFF); break;
    case SaveType::Eeprom: save.assign(0x2000, 0xFF); break;
    default: save.clear(); break;
    }
}

void Cart::resetState() {
    flash = FlashState{};
    eeprom = EepromState{};
    gpio = GpioState{};
    rtc = RtcState{};
}

std::string Cart::title() const {
    if (rom.size() < 0xB0) return {};
    std::string t(reinterpret_cast<const char*>(&rom[0xA0]), 12);
    t.erase(std::find(t.begin(), t.end(), '\0'), t.end());
    while (!t.empty() && t.back() == ' ') t.pop_back();
    return t;
}

std::string Cart::gameCode() const {
    if (rom.size() < 0xB0) return {};
    std::string c(reinterpret_cast<const char*>(&rom[0xAC]), 4);
    c.erase(std::find(c.begin(), c.end(), '\0'), c.end());
    return c;
}

// ---------------------------------------------------------------- SRAM / Flash
u8 Cart::readBackup(u32 addr) {
    switch (saveType) {
    case SaveType::Sram:
        return save[addr & 0x7FFF];
    case SaveType::Flash64:
    case SaveType::Flash128: {
        u32 a = addr & 0xFFFF;
        if (flash.idMode && a < 2) {
            // 64K: Panasonic MN63F805MNP, 128K: Sanyo LE26FV10N1TS
            if (saveType == SaveType::Flash64) return a == 0 ? 0x32 : 0x1B;
            return a == 0 ? 0x62 : 0x13;
        }
        return save[(u32(flash.bank) << 16) + a];
    }
    default:
        return 0xFF;
    }
}

void Cart::writeBackup(u32 addr, u8 v) {
    if (saveType == SaveType::Sram) {
        save[addr & 0x7FFF] = v;
        dirty = true;
        return;
    }
    if (saveType != SaveType::Flash64 && saveType != SaveType::Flash128) return;

    u32 a = addr & 0xFFFF;
    if (flash.writeNext) {
        save[(u32(flash.bank) << 16) + a] = v;
        flash.writeNext = false;
        dirty = true;
        return;
    }
    if (flash.bankNext) {
        if (a == 0) flash.bank = v & 1;
        flash.bankNext = false;
        return;
    }
    if (v == 0xF0 && flash.state == 0) {
        flash.idMode = false;
        return;
    }
    switch (flash.state) {
    case 0:
        if (a == 0x5555 && v == 0xAA) flash.state = 1;
        break;
    case 1:
        flash.state = (a == 0x2AAA && v == 0x55) ? 2 : 0;
        break;
    default:
        flash.state = 0;
        if (flash.erasePrep) {
            if (a == 0x5555 && v == 0x10) {
                std::fill(save.begin(), save.end(), 0xFF);
                dirty = true;
            } else if (v == 0x30) {
                u32 base = (u32(flash.bank) << 16) + (a & 0xF000);
                std::fill(save.begin() + base, save.begin() + base + 0x1000, 0xFF);
                dirty = true;
            }
            flash.erasePrep = false;
            break;
        }
        if (a != 0x5555) break;
        switch (v) {
        case 0x90: flash.idMode = true; break;
        case 0xF0: flash.idMode = false; break;
        case 0x80: flash.erasePrep = true; break;
        case 0xA0: flash.writeNext = true; break;
        case 0xB0: if (saveType == SaveType::Flash128) flash.bankNext = true; break;
        default: break;
        }
        break;
    }
}

// ---------------------------------------------------------------- EEPROM
bool Cart::isEepromAddr(u32 addr) const {
    if (saveType != SaveType::Eeprom) return false;
    if (rom.size() > 0x1000000) return addr >= 0x0DFFFF00 && addr < 0x0E000000;
    return (addr >> 24) == 0x0D;
}

void Cart::eepromSetSizeFromDma(u32 count) {
    if (eeprom.addrBits) return;
    if (count == 9 || count == 73) eeprom.addrBits = 6;
    else if (count == 17 || count == 81) eeprom.addrBits = 14;
}

u16 Cart::eepromRead() {
    if (eeprom.mode != 2) return 1;
    int pos = eeprom.readPos++;
    if (eeprom.readPos >= 68) eeprom.mode = 0;
    if (pos < 4) return 0;
    return u16((eeprom.data >> (63 - (pos - 4))) & 1);
}

void Cart::eepromWrite(u16 v) {
    if (eeprom.addrBits == 0) eeprom.addrBits = 14;
    if (eeprom.mode == 2) eeprom.mode = 0;
    eeprom.bits = (eeprom.bits << 1) | (v & 1);
    eeprom.bitCount++;
    int ab = eeprom.addrBits;
    u32 mask = ab == 6 ? 0x3F : 0x3FF;

    if (eeprom.bitCount == 2) {
        eeprom.cmd = int(eeprom.bits & 3);
        if (eeprom.cmd < 2) { eeprom.bits = 0; eeprom.bitCount = 0; }
        return;
    }
    if (eeprom.cmd == 3) { // read request: 11 + address + 0
        if (eeprom.bitCount == 2 + ab + 1) {
            eeprom.address = u32(eeprom.bits >> 1) & mask;
            eeprom.data = 0;
            for (int i = 0; i < 8; i++) eeprom.data = (eeprom.data << 8) | save[eeprom.address * 8 + i];
            eeprom.mode = 2;
            eeprom.readPos = 0;
            eeprom.bits = 0;
            eeprom.bitCount = 0;
        }
    } else if (eeprom.cmd == 2) { // write: 10 + address + 64 data bits + 0
        if (eeprom.bitCount == 2 + ab) eeprom.address = u32(eeprom.bits) & mask;
        if (eeprom.bitCount == 2 + ab + 64) eeprom.data = eeprom.bits;
        if (eeprom.bitCount == 2 + ab + 65) {
            eepromCommit();
            eeprom.bits = 0;
            eeprom.bitCount = 0;
        }
    }
}

void Cart::eepromCommit() {
    for (int i = 0; i < 8; i++) save[eeprom.address * 8 + i] = u8(eeprom.data >> (56 - 8 * i));
    dirty = true;
}

// ---------------------------------------------------------------- GPIO + RTC
// Pins: bit 0 SCK, bit 1 SIO, bit 2 CS. A transfer starts with CS low/SCK high, then CS high.
// Each bit is presented on SIO while SCK is low and taken on the SCK rising edge, LSB first.
// The first byte is the command: low nibble 0110, bits 4-6 the register, bit 7 = read.

static const int kRtcBytes[8] = {0, 0, 7, 0, 1, 0, 3, 0};

static u8 toBcd(int v) { return u8(((v / 10) % 10) << 4 | (v % 10)); }

bool Cart::gpioRead(u32 addr, u16& v) const {
    if (!gpio.readable) return false;
    switch (addr & 0x01FFFFFE) {
    case 0xC4: v = gpio.pins; return true;
    case 0xC6: v = gpio.direction; return true;
    case 0xC8: v = gpio.readable; return true;
    default: return false;
    }
}

void Cart::gpioWrite(u32 addr, u16 v) {
    switch (addr & 0x01FFFFFE) {
    case 0xC4:
        gpio.pins = u8((gpio.pins & ~gpio.direction) | (v & gpio.direction & 0xF));
        rtcPins();
        break;
    case 0xC6: gpio.direction = u8(v & 0xF); break;
    case 0xC8: gpio.readable = u8(v & 1); break;
    default: break;
    }
}

void Cart::gpioOutput(u8 pins) {
    gpio.pins = u8((gpio.pins & gpio.direction) | (pins & ~gpio.direction & 0xF));
}

void Cart::rtcLatchTime() {
    std::time_t now = std::time(nullptr);
    std::tm t = *std::localtime(&now);
    rtc.time[0] = toBcd(t.tm_year % 100);
    rtc.time[1] = toBcd(t.tm_mon + 1);
    rtc.time[2] = toBcd(t.tm_mday);
    rtc.time[3] = toBcd(t.tm_wday);
    if (rtc.control & 0x40) rtc.time[4] = toBcd(t.tm_hour);
    else rtc.time[4] = u8(toBcd(t.tm_hour % 12) | (t.tm_hour >= 12 ? 0x80 : 0));
    rtc.time[5] = toBcd(t.tm_min);
    rtc.time[6] = toBcd(t.tm_sec);
}

void Cart::rtcByte() {
    if (!rtc.active) {
        if ((rtc.bits & 0x0F) == 0x06) {
            rtc.command = rtc.bits;
            int reg = (rtc.bits >> 4) & 7;
            rtc.bytesLeft = kRtcBytes[reg];
            rtc.active = rtc.bytesLeft > 0;
            if (reg == 0) rtc.control = 0;
            if (reg == 2 || reg == 6) rtcLatchTime();
        }
    } else {
        if (((rtc.command >> 4) & 7) == 4) rtc.control = rtc.bits;
        if (--rtc.bytesLeft <= 0) { rtc.active = false; rtc.command = 0; }
    }
    rtc.bits = 0;
    rtc.bitsRead = 0;
}

void Cart::rtcPins() {
    u8 p = gpio.pins;
    switch (rtc.step) {
    case 0:
        if ((p & 5) == 1) rtc.step = 1;
        break;
    case 1:
        if ((p & 5) == 5) rtc.step = 2;
        else if ((p & 5) != 1) rtc.step = 0;
        break;
    case 2:
        if (!(p & 1)) { // SCK low: the console presents the next bit
            rtc.bits = u8((rtc.bits & ~(1 << rtc.bitsRead)) | (((p >> 1) & 1) << rtc.bitsRead));
        } else if (p & 4) { // SCK rising edge with CS high
            bool reading = rtc.active && (rtc.command & 0x80);
            if (!reading) {
                if (++rtc.bitsRead == 8) rtcByte();
            } else {
                u8 byte = 0;
                switch ((rtc.command >> 4) & 7) {
                case 4: byte = rtc.control; break;
                case 2: byte = rtc.time[7 - rtc.bytesLeft]; break;
                case 6: byte = rtc.time[7 - rtc.bytesLeft]; break;
                default: break;
                }
                gpioOutput(u8(5 | (((byte >> rtc.bitsRead) & 1) << 1)));
                if (++rtc.bitsRead == 8) {
                    rtc.bitsRead = 0;
                    if (--rtc.bytesLeft <= 0) { rtc.active = false; rtc.command = 0; }
                }
            }
        } else { // CS dropped: end of transfer
            rtc.bitsRead = 0;
            rtc.bytesLeft = 0;
            rtc.active = false;
            rtc.command = 0;
            rtc.step = p & 1;
            gpioOutput(1);
        }
        break;
    }
}

} // namespace mulator
