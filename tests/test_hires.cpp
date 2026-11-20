// High-resolution rendering: the CEL engine drawing onto a finer grid while
// VRAM stays exactly as the hardware would leave it.
#include "core/bus.h"
#include "core/console.h"
#include "core/hires.h"
#include "core/madam.h"
#include "core/vdlp.h"
#include "test_harness.h"

using namespace retro3do;

namespace {

constexpr s32 fixed(int whole) { return static_cast<s32>(whole) << 16; }
constexpr u16 kWhite = 0x7fffu & ~1u;

constexpr u32 kCcbAt = 0x1000;
constexpr u32 kSourceAt = 0x2000;
constexpr u32 kListAt = 0x8000;

// An opaque copy: multiply by eight, divide by eight.
constexpr u32 kPixcCopy = 0x1f001f00u;

// A 4x4 white direct-colour cel, rotated forty-five degrees and scaled by
// `scale_fixed`, placed at a fractional position so its edges fall between
// logical pixels.
void draw_diamond(Bus& bus, s32 x, s32 y, s32 step, u16 colour = kWhite) {
    for (u32 row = 0; row < 4; ++row) {
        for (u32 col = 0; col < 4; ++col) {
            bus.write16(kSourceAt + row * 8 + col * 2, colour);
        }
    }
    bus.write32(kCcbAt + 0, kCcbLast | kCcbNpAbs | kCcbSpAbs | kCcbPpAbs |
                                kCcbLdSize | kCcbLdPrs | kCcbLdPpmp |
                                kCcbLdPlut | kCcbCcbPre | kCcbYoxy | kCcbAcw |
                                kCcbAccw);
    bus.write32(kCcbAt + 4, 0);
    bus.write32(kCcbAt + 8, kSourceAt);
    bus.write32(kCcbAt + 12, 0);
    bus.write32(kCcbAt + 16, static_cast<u32>(x));
    bus.write32(kCcbAt + 20, static_cast<u32>(y));
    bus.write32(kCcbAt + 24, static_cast<u32>(step << 4));    // hdx
    bus.write32(kCcbAt + 28, static_cast<u32>(step << 4));    // hdy
    bus.write32(kCcbAt + 32, static_cast<u32>(-step));        // vdx
    bus.write32(kCcbAt + 36, static_cast<u32>(step));         // vdy
    bus.write32(kCcbAt + 40, 0);
    bus.write32(kCcbAt + 44, 0);
    bus.write32(kCcbAt + 48, kPixcCopy);
    bus.write32(kCcbAt + 52, 6u | 0x10u | (3u << 6));         // direct, linear, 4 rows
    bus.write32(kCcbAt + 56, 3u | 0x00001000u);               // 4 wide

    bus.write32(kMadamBase + kMadamNextCcb, kCcbAt);
    bus.write32(kMadamBase + kMadamCelStart, 0);
    bus.run_pending_cel_engine();
}

void configure_framebuffer(Bus& bus) {
    bus.write32(kMadamBase + kMadamRegCtl0, 0x00005050u);
    bus.write32(kMadamBase + kMadamRegCtl2, kVramBase);
    bus.write32(kMadamBase + kMadamRegCtl3, kVramBase);
}

void point_display_at_vram(Bus& bus) {
    bus.write32(kListAt + 0, kVdlEnableDma | kVdlCurrOverride | 261u);
    bus.write32(kListAt + 4, kVramBase);
    bus.write32(kListAt + 8, kVramBase);
    bus.write32(kListAt + 12, 0);
    bus.write32(kMadamBase + kMadamVdlAddress, kListAt);
}

// Draw the same diamond on a machine running at `scale` and return its frame.
std::vector<u32> render_at(u32 scale, int* width, int* height,
                           u32* vram_checksum) {
    Console console;
    console.set_render_scale(scale);
    console.reset();
    Bus& bus = console.bus();
    console.run_frame();   // the scale takes effect at a frame boundary

    configure_framebuffer(bus);
    point_display_at_vram(bus);
    draw_diamond(bus, fixed(60) + 0x8000, fixed(40) + 0x4000, fixed(3) + 0x2000);
    console.run_frame();

    u32 sum = 0;
    for (u32 i = 0; i < 320u * 240u * 2u; ++i) {
        sum = sum * 31u + bus.vram()[i];
    }
    *vram_checksum = sum;

    const Frame frame = console.framebuffer();
    *width = frame.width;
    *height = frame.height;
    return std::vector<u32>(frame.pixels,
                            frame.pixels + static_cast<size_t>(frame.width) *
                                               frame.height);
}

}  // namespace

TEST(the_render_scale_sets_the_size_of_the_frame) {
    Console console;
    console.reset();
    CHECK_EQ(console.framebuffer().width, 640);

    for (u32 scale = 2; scale <= HiresStore::kMaxScale; ++scale) {
        console.set_render_scale(scale);
        console.run_frame();
        CHECK_EQ(console.framebuffer().width, static_cast<int>(320 * scale));
        CHECK_EQ(console.framebuffer().height, static_cast<int>(240 * scale));
    }

    console.set_render_scale(99);
    CHECK_EQ(console.render_scale(), HiresStore::kMaxScale);
    console.set_render_scale(1);
    console.run_frame();
    CHECK_EQ(console.framebuffer().width, 640);
}

TEST(high_resolution_leaves_vram_exactly_as_the_hardware_draws_it) {
    // Titles read the framebuffer back, so the finer pass must never change
    // what is in VRAM.
    int w = 0, h = 0;
    u32 native = 0, fine = 0;
    render_at(1, &w, &h, &native);
    render_at(4, &w, &h, &fine);
    CHECK_EQ(native, fine);
}

TEST(a_rotated_cel_gets_sub_pixel_edges) {
    int w1 = 0, h1 = 0, w2 = 0, h2 = 0;
    u32 sum = 0;
    const std::vector<u32> native = render_at(1, &w1, &h1, &sum);
    const std::vector<u32> fine = render_at(2, &w2, &h2, &sum);
    CHECK_EQ(w1, 640);
    CHECK_EQ(w2, 640);

    // The hardware picture is made of 2x2 blocks (this display list does not
    // interpolate). The fine one must have edge blocks that are split.
    int split_native = 0, split_fine = 0, lit_native = 0, lit_fine = 0;
    for (int y = 0; y + 1 < h1; y += 2) {
        for (int x = 0; x + 1 < w1; x += 2) {
            const auto split = [&](const std::vector<u32>& f) {
                const u32 a = f[y * w1 + x];
                return a != f[y * w1 + x + 1] || a != f[(y + 1) * w1 + x] ||
                       a != f[(y + 1) * w1 + x + 1];
            };
            split_native += split(native) ? 1 : 0;
            split_fine += split(fine) ? 1 : 0;
        }
    }
    for (size_t i = 0; i < native.size(); ++i) {
        lit_native += (native[i] & 0x00ffffffu) != 0 ? 1 : 0;
        lit_fine += (fine[i] & 0x00ffffffu) != 0 ? 1 : 0;
    }
    CHECK_EQ(split_native, 0);
    CHECK(split_fine > 10);
    // Roughly the same figure: the finer edges trim and add a little, they do
    // not change its size.
    CHECK(lit_fine > lit_native / 2);
    CHECK(lit_fine < lit_native * 2);
}

TEST(a_pixel_the_cpu_overwrites_falls_back_to_the_hardware_picture) {
    Console console;
    console.set_render_scale(3);
    console.reset();
    Bus& bus = console.bus();
    console.run_frame();

    configure_framebuffer(bus);
    point_display_at_vram(bus);
    draw_diamond(bus, fixed(60) + 0x8000, fixed(40) + 0x4000, fixed(3) + 0x2000);

    // Overwrite the whole region the diamond covers with a flat colour, as a
    // title drawing video over it would.
    const u16 blue = 0x001fu;
    for (int y = 30; y < 70; ++y) {
        for (int x = 40; x < 90; ++x) {
            bus.write16(kVramBase + framebuffer_offset(x, y, 320), blue);
        }
    }
    console.run_frame();

    const Frame frame = console.framebuffer();
    const int top = static_cast<int>(console.vdlp().buffer_start_line());
    for (int y = 30; y < 70; ++y) {
        const int screen_y = y - top;
        if (screen_y < 0) continue;
        for (int x = 40; x < 90; ++x) {
            for (int s = 0; s < 9; ++s) {
                const int fx = x * 3 + s % 3;
                const int fy = screen_y * 3 + s / 3;
                const u32 px = frame.pixels[fy * frame.width + fx];
                // Pure blue through the default CLUT, never white detail.
                CHECK((px & 0x00ffff00u) == 0);
            }
        }
    }
}

namespace {

// White, then a dark copy averaged over it: (src + frame) / 2.
// Returns the colour at the middle of the figure, top-left fine pixel.
u32 blended_centre(u32 scale) {
    Console console;
    console.set_render_scale(scale);
    console.reset();
    Bus& bus = console.bus();
    console.run_frame();
    configure_framebuffer(bus);
    point_display_at_vram(bus);

    draw_diamond(bus, fixed(100), fixed(60), fixed(4));
    // S1=0, S2=2 (the frame), MS=0, MXF=7, DV1=3 (x1), DV2=1 (halve the sum).
    const u32 average = (2u << 6) | (3u << 8) | (7u << 10) | 1u;
    const u16 dark = static_cast<u16>((1u << 10) | (1u << 5) | 1u);
    for (u32 row = 0; row < 4; ++row) {
        for (u32 col = 0; col < 4; ++col) {
            bus.write16(kSourceAt + row * 8 + col * 2, dark);
        }
    }
    bus.write32(kCcbAt + 48, average | (average << 16));
    bus.write32(kMadamBase + kMadamNextCcb, kCcbAt);
    bus.write32(kMadamBase + kMadamCelStart, 0);
    bus.run_pending_cel_engine();
    console.run_frame();

    const Frame frame = console.framebuffer();
    const int out = scale > 1 ? static_cast<int>(scale) : 2;
    const int y = 76 - static_cast<int>(console.vdlp().buffer_start_line());
    return frame.pixels[(y * out) * frame.width + 100 * out];
}

}  // namespace

TEST(a_blended_cel_blends_against_the_fine_picture_once) {
    // A translucent cel must blend each sub-pixel against what was there
    // before it, not against its own output: a double blend darkens it again.
    const u32 native = blended_centre(1);
    CHECK((native & 0x00ffffffu) != 0x00ffffffu);   // it did blend
    CHECK_EQ(blended_centre(2), native);
    CHECK_EQ(blended_centre(4), native);
}
