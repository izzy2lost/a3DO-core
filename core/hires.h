// High-resolution rendering: a sub-pixel shadow of VRAM.
//
// The 3DO's CEL engine draws into a 320-wide framebuffer, and the display
// generator then doubles it to 640 by copying or averaging. Every edge of every
// polygon is therefore quantised to the logical grid before the picture is
// ever enlarged. Rendering "at a higher resolution" means drawing the cels a
// second time onto a finer grid, so a rotated or perspective-stepped cel lands
// with its true sub-pixel edges.
//
// That finer picture cannot replace VRAM: titles read the framebuffer back,
// copy it with the SPORT, blend against it and write it with the CPU. So VRAM
// is still drawn exactly as the hardware draws it, and alongside it each VRAM
// pixel owns scale * scale sub-pixels here. A slot's sub-pixels are only used
// for display while they still describe what is in VRAM:
//
//   - every bus write to VRAM (CPU, SPORT, DMA) clears the slot's valid flag;
//   - the CEL engine re-validates a slot after it writes it, recording the
//     value it wrote as the slot's echo;
//   - the display uses the sub-pixels only if the slot is valid AND VRAM still
//     holds the echo.
//
// Anything else - full-motion video drawn by the CPU, a title poking pixels -
// falls back to the ordinary hardware image for exactly the pixels concerned.
//
// Only VRAM is shadowed. A framebuffer placed anywhere else simply renders at
// the hardware resolution.
#pragma once

#include <vector>

#include "types.h"

namespace retro3do {

class HiresStore {
public:
    static constexpr u32 kMaxScale = 6;
    static constexpr u32 kVramBytes = 1u * 1024 * 1024;
    static constexpr u32 kSlots = kVramBytes / 2u;

    // 1 frees everything and turns the shadow off.
    void configure(u32 scale);
    u32 scale() const { return scale_; }
    bool active() const { return scale_ > 1; }

    // Forget every slot, as a reset does. Sub-pixels are left as they are;
    // nothing reads them while their slot is invalid.
    void clear();

    // The slot of a byte offset into VRAM.
    static u32 slot_of(u32 vram_offset) {
        return (vram_offset & (kVramBytes - 1u)) >> 1;
    }

    u16* subpixels(u32 slot) { return &subs_[static_cast<size_t>(slot) * area_]; }
    const u16* subpixels(u32 slot) const {
        return &subs_[static_cast<size_t>(slot) * area_];
    }

    bool usable(u32 slot, u16 vram_pixel) const {
        return valid_[slot] != 0 && echo_[slot] == vram_pixel;
    }

    // A write reached VRAM from somewhere other than the CEL engine's own
    // sub-pixel aware path.
    void invalidate(u32 vram_offset, u32 bytes) {
        const u32 first = slot_of(vram_offset);
        const u32 last = slot_of(vram_offset + bytes - 1u);
        valid_[first] = 0;
        valid_[last] = 0;
    }

    // The CEL engine wrote `value` to the slot and its sub-pixels describe it.
    void validate(u32 slot, u16 value) {
        echo_[slot] = value;
        valid_[slot] = 1;
    }

    void fill(u32 slot, u16 value) {
        u16* s = subpixels(slot);
        for (u32 i = 0; i < area_; ++i) s[i] = value;
    }

    // Which cel last claimed the slot. A slot is initialised from VRAM the
    // first time a cel's high-resolution pass touches it, and not again for
    // the rest of that cel, so blending reads what was there before the cel.
    u32 stamp(u32 slot) const { return stamp_[slot]; }
    void set_stamp(u32 slot, u32 serial) { stamp_[slot] = serial; }
    void clear_stamps();

private:
    u32 scale_ = 1;
    u32 area_ = 1;
    std::vector<u16> subs_;
    std::vector<u16> echo_;
    std::vector<u8> valid_;
    std::vector<u32> stamp_;
};

}  // namespace retro3do
