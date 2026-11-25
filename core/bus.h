// System bus and memory map.
//
// Everything the CPU can address goes through here. The map below is written
// from published 3DO hardware documentation and is deliberately data-driven, so
// that correcting a region is a one-line change rather than a hunt through
// scattered address comparisons.
//
// Region bases still to be confirmed against the Portfolio hardware manuals are
// marked TODO(map). They are not guesses in the sense of being invented — they
// are the commonly published values — but this core's rule is that nothing is
// asserted as fact until it has been checked against a source we can cite.
#pragma once

#include <vector>

#include "types.h"

namespace retro3do {

class StateIO;

class Madam;
class HiresStore;
class Clio;
class Sport;

// ---------------------------------------------------------------------------
// Memory map
// ---------------------------------------------------------------------------
enum : u32 {
    // The machine's real map: two megabytes of DRAM in one run - DRAM1 at
    // 0x00000000, DRAM2 at 0x00100000 - with a megabyte of VRAM above at
    // 0x00200000.
    //
    // This was halved for a long time, with VRAM moved down to 0x00100000,
    // because the correct layout made the ROM panic. It turned out not to be
    // the map at all: MADAM's memory-configuration register was writable, the
    // ROM writes zero to it during start-up, and its own sizing routine then
    // read back "no memory fitted" and gave up. See kMadamMemConfigStock.
    //
    kDramBase = 0x00000000,
    kDramSize = 2u * 1024 * 1024,

    kVramBase = 0x00200000,
    kVramSize = 1u * 1024 * 1024,

    kRomBase  = 0x03000000,         // BIOS. Also the reset vector.
    kRomSize  = 1u * 1024 * 1024,

    // 32 KiB of storage occupying a 128 KiB window.
    //
    // The ARM60 has no bus address translator, so the NVRAM's data lines sit on
    // the low byte of the word and each stored byte is addressed as a whole
    // 32-bit word - byte n lives at base + n*4. Mapping it flat instead makes
    // the machine see four times as much NVRAM as exists, with every byte in
    // the wrong place.
    // A second copy answers 0x20000 higher; the register map lists it as "a
    // mirror of the above" and MAME maps it with .mirror(0x20000).
    kNvramMirror = 0x00020000,
    kNvramBase   = 0x03140000,
    kNvramSize   = 32u * 1024,          // real storage
    kNvramStride = 4u,                  // bytes of address space per stored byte
    kNvramWindow = kNvramSize * kNvramStride,
    kNvramSpan   = kNvramMirror + kNvramWindow,

    // SPORT: the VRAM serial port, which does fast page copies and clears.
    //
    // Found by tracing the boot ROM's memory test, which builds addresses as
    // `0x03200000 | (address >> 9)` and writes to them - the classic
    // "the address IS the command" encoding. The test fills memory with an
    // arithmetic pattern, waits for video lines 10..13, drives SPORT, then
    // reads back and compares; the compare fails here because nothing is
    // mapped at this address, so the writes go nowhere.
    //
    // The copy, fill-value and fill windows are implemented in Sport. Accesses
    // remain counted as a diagnostic because the ROM's own memory test is the
    // fastest way to spot a broken decode here.
    kSportBase = 0x03200000,
    // SPORT decodes only the windows it actually has: the copy window at
    // +0x0000, the fill-value register at +0x2000, and the fill window at
    // +0x4000. Claiming the whole megabyte the region is listed as means every
    // stray access anywhere in it is treated as a page copy - and the machine
    // does make them, repeatedly, near the top of the region. Under the real
    // memory map that turns into 142 million page copies and looks like a hang.
    kSportSize = 0x00010000,

    kMadamBase = 0x03300000,        // confirmed by the boot ROM
    kMadamSize = 0x00100000,

    kClioBase  = 0x03400000,        // confirmed by the boot ROM
    kClioSize  = 0x00100000,
};

// A write that lands in memory the CPU might later execute has to invalidate
// the decode cache. Rather than have the bus know about the CPU, it records
// that something happened and the console asks.
// Which pages of memory have been written since the CPU's decode cache was last
// brought up to date.
//
// This used to be a single low/high range, which is wrong in a way that only
// shows up as slowness: two writes at opposite ends of memory in the same slice
// make the range cover everything between them, and the whole span then gets
// invalidated page by page. The OS legitimately writes across the whole of DRAM,
// so the range was usually enormous, and the cost scaled with how much memory
// the machine had rather than with how much was actually written.
//
// A set of dirty pages costs the same to record and makes invalidation
// proportional to what was really touched.
struct WriteWatch {
    static constexpr u32 kPageBytes = 4096;
    static constexpr u32 kPages     = (2u * 1024 * 1024) / kPageBytes;
    static constexpr u32 kWords     = (kPages + 63u) / 64u;

    bool dirty = false;
    u64  bits[kWords] = {};

    void note(u32 address, u32 length) {
        if (length == 0) return;
        const u32 first = address / kPageBytes;
        const u32 last  = (address + length - 1) / kPageBytes;
        for (u32 page = first; page <= last && page < kPages; ++page) {
            bits[page >> 6] |= (u64{1} << (page & 63));
        }
        dirty = true;
    }

    bool is_dirty(u32 page) const {
        return page < kPages && (bits[page >> 6] >> (page & 63) & 1) != 0;
    }

    void clear() {
        dirty = false;
        for (u64& word : bits) word = 0;
    }
};

class Bus {
public:
    Bus();

    Bus(const Bus&) = delete;
    Bus& operator=(const Bus&) = delete;

    // Save or restore everything this part of the machine holds (state.cpp).
    void serialize(StateIO& s);
    void reset();

    void attach_madam(Madam* madam) { madam_ = madam; }
    void attach_clio(Clio* clio) { clio_ = clio; }
    void attach_sport(Sport* sport) { sport_ = sport; }
    // The high-resolution shadow, when it is on. Every VRAM store through the
    // bus marks the sub-pixels of the pixels it touched as stale.
    void attach_hires(HiresStore* hires) { hires_ = hires; }

    // Load a BIOS image. Returns false if the image is missing or too large.
    bool load_bios(const u8* data, size_t size);
    bool bios_loaded() const { return bios_loaded_; }

    // --- CPU-visible access ----------------------------------------------
    // The 3DO is big-endian as the CPU sees it; these do the byte order.
    //
    // Plain DRAM and VRAM are handled here, inline, because every pixel the
    // cel engine draws and most of what the CPU and DSP touch is one of them,
    // and a call per byte was a measurable share of a slow phone's frame.
    // Everything else - the ROM overlay, NVRAM, the chips, the hi-res shadow -
    // goes to the full decode in bus.cpp, which handles every address on its
    // own and is the reference for what the fast paths must match.
    u32 read32(u32 address) {
        const u32 addr = address & ~u32{3};
        if (const u8* p = fast_read(addr)) return load32(p);
        return read32_slow(address);
    }
    u16 read16(u32 address) {
        const u32 addr = address & ~u32{1};
        if (const u8* p = fast_read(addr)) return static_cast<u16>((p[0] << 8) | p[1]);
        return read16_slow(address);
    }
    u8 read8(u32 address) {
        if (const u8* p = fast_read(address)) return *p;
        return read8_slow(address);
    }

    void write32(u32 address, u32 value) {
        const u32 addr = address & ~u32{3};
        if (u8* p = fast_write(addr, 4)) {
            p[0] = static_cast<u8>(value >> 24); p[1] = static_cast<u8>(value >> 16);
            p[2] = static_cast<u8>(value >> 8);  p[3] = static_cast<u8>(value);
            return;
        }
        write32_slow(address, value);
    }
    void write16(u32 address, u16 value) {
        const u32 addr = address & ~u32{1};
        if (u8* p = fast_write(addr, 2)) {
            p[0] = static_cast<u8>(value >> 8); p[1] = static_cast<u8>(value);
            return;
        }
        write16_slow(address, value);
    }
    void write8(u32 address, u8 value) {
        if (u8* p = fast_write(address, 1)) { *p = value; return; }
        write8_slow(address, value);
    }

    // Whether a read here is plain DRAM or VRAM, with no side effects.
    bool is_plain_memory(u32 address) const { return fast_read(address) != nullptr; }

    // Instruction fetch. Separate from read32 because it is the hottest path
    // in the machine and because a prefetch abort is a different exception
    // from a data abort.
    u32 fetch32(u32 address);

    // --- direct access for the chips --------------------------------------
    // MADAM and the VDLP walk VRAM far too often to pay bus dispatch for it.
    // Put the NVRAM back to formatted-but-empty. Not the same as zeroing it:
    // a zeroed NVRAM has no filesystem, and software that finds none reports
    // it as full rather than formatting it.
    void format_nvram();

    // The NVRAM as bytes, for saving to and restoring from a file. It is one
    // byte per 32-bit word as the CPU sees it, so this is kNvramSize long and
    // not the 128KiB the address window covers.
    //
    // A restored image is taken as-is rather than being validated: a console
    // with a corrupt NVRAM shows the user its own error message about it, and
    // rewriting it behind their back would hide a real fault and lose real
    // saves. The only check is the length.
    const u8* nvram() const { return nvram_.data(); }
    size_t nvram_size() const { return nvram_.size(); }
    bool restore_nvram(const u8* data, size_t size);

    // Set by any write to the NVRAM, cleared when the caller has saved it.
    // Saving thirty-two kilobytes every frame would be wasteful and would
    // wear a phone's flash for no reason; titles write it rarely.
    bool nvram_dirty() const { return nvram_dirty_; }
    void clear_nvram_dirty() { nvram_dirty_ = false; }

    u8* dram() { return dram_.data(); }
    u8* vram() { return vram_.data(); }
    const u8* dram() const { return dram_.data(); }
    const u8* vram() const { return vram_.data(); }

    WriteWatch& write_watch() { return write_watch_; }

    // The cel engine is asked to start by a store, but it does not start
    // during that store. See run_pending_cel_engine.
    void request_cel_engine() { cel_pending_ = true; }
    void cancel_cel_engine() { cel_pending_ = false; }
    bool cel_engine_pending() const { return cel_pending_; }
    void run_pending_cel_engine();

    // True for any address that belongs to a chip rather than to memory. Byte
    // and halfword accesses to those go through a read-modify-write of the
    // containing word, because the registers are word-wide.
    static bool is_device(u32 address) {
        return (address >= kSportBase && address < kSportBase + kSportSize) ||
               (address >= kMadamBase && address < kMadamBase + kMadamSize) ||
               (address >= kClioBase && address < kClioBase + kClioSize);
    }

    // SPORT diagnostics. A silent decode failure is difficult to attribute,
    // so all accesses remain counted even though the operations are present.
    u64 sport_accesses() const { return sport_accesses_; }

    // Whether the low memory still reads ROM. Cleared by the first write there.
    bool rom_overlay() const { return rom_overlay_; }

private:
    static u32 load32(const u8* p) {
        return (static_cast<u32>(p[0]) << 24) | (static_cast<u32>(p[1]) << 16) |
               (static_cast<u32>(p[2]) << 8) | static_cast<u32>(p[3]);
    }
    // Where a read of plain memory lands, or null for anything the full decode
    // must see: low memory while the ROM still overlays it, and every address
    // that is not DRAM or VRAM.
    const u8* fast_read(u32 addr) const {
        if (addr < kDramSize) return rom_overlay_ ? nullptr : dram_.data() + addr;
        if (addr - kVramBase < kVramSize) return vram_.data() + addr - kVramBase;
        return nullptr;
    }
    // The same for a store, which does here what the full decode would: a
    // DRAM store ends the ROM overlay and is noted for the decode cache. A
    // VRAM store with the hi-res shadow on goes the long way, to invalidate it.
    u8* fast_write(u32 addr, u32 length) {
        if (addr < kDramSize) {
            rom_overlay_ = false;
            write_watch_.note(addr, length);
            return dram_.data() + addr;
        }
        if (addr - kVramBase < kVramSize && hires_ == nullptr) {
            return vram_.data() + addr - kVramBase;
        }
        return nullptr;
    }

    u32  read32_slow(u32 address);
    u16  read16_slow(u32 address);
    u8   read8_slow(u32 address);
    void write32_slow(u32 address, u32 value);
    void write16_slow(u32 address, u16 value);
    void write8_slow(u32 address, u8 value);

    // At reset the low two megabytes read ROM rather than DRAM, and the FIRST
    // WRITE anywhere in that range takes the overlay away for good - the ROM
    // shadows itself down there, then writes to it and carries on running from
    // the DRAM underneath.
    //
    // It is a write that removes it, not a register: guessing at a control bit
    // instead kills the boot after 197 instructions.
    bool rom_overlay_ = true;

    std::vector<u8> dram_;
    std::vector<u8> vram_;
    std::vector<u8> rom_;
    std::vector<u8> nvram_;
    bool nvram_dirty_ = false;

    bool bios_loaded_ = false;

    Madam* madam_ = nullptr;
    HiresStore* hires_ = nullptr;
    Clio*  clio_  = nullptr;
    Sport* sport_ = nullptr;

    WriteWatch write_watch_;
    bool cel_pending_ = false;
    u64 sport_accesses_ = 0;
};

}  // namespace retro3do
