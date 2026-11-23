// The C ABI, implemented over the C++ machine.
//
// There is deliberately no logic here beyond translation. Anything that makes a
// decision belongs in the core proper, where the tests can reach it; if a
// function in this file grows a branch that is not "did that fail, and what do I
// say about it", the behaviour has been put in the wrong place.
#include "retro3do.h"

#include <cstdio>
#include <functional>
#include <cstring>
#include <string>
#include <vector>

#include "core/arm60.h"
#include "core/audio_ring.h"
#include "core/bus.h"
#include "core/console.h"
#include "core/disc.h"
#include "core/emulator_thread.h"
#include "core/frame_mailbox.h"
#include "core/pad.h"

using namespace retro3do;

// The handle. Declaration order matters: the runner holds references to the
// console and the mailbox, so both must be constructed before it and destroyed
// after it.
struct retro3do_machine {
    Console console;
    FrameMailbox mailbox;
    EmulatorThread thread{console, mailbox};

    std::string error;
    std::vector<u8> nvram_scratch;
};

namespace {

// Everything here answers a caller holding a const handle, but the state being
// read is atomic and owned by the emulator thread. Constness at this boundary
// means "will not change the machine", not "touches nothing mutable".
retro3do_t *mut(const retro3do_t *emu) { return const_cast<retro3do_t *>(emu); }

int fail(retro3do_t *emu, const char *what) {
    emu->error = what;
    return 0;
}

// Take the machine's last complaint if it left one, otherwise say something
// rather than handing back an empty string that reads as "no error".
int fail_from_console(retro3do_t *emu, const char *fallback) {
    emu->error = emu->console.last_error().empty() ? fallback
                                                   : emu->console.last_error();
    return 0;
}

// Copy the finished field into the mailbox and publish it. The emulator thread
// does this itself; this exists so retro3do_run_frame behaves identically for a
// harness driving the machine by hand.
void publish_frame(retro3do_t *emu) {
    const Frame frame = emu->console.framebuffer();
    if (frame.pixels == nullptr) {
        return;
    }
    std::memcpy(emu->mailbox.writable(frame.width, frame.height), frame.pixels,
                static_cast<size_t>(frame.width) * frame.height * sizeof(u32));
    emu->mailbox.publish();
}

bool valid_pad(uint32_t pad) { return pad < kMaxPads; }

PadButton to_button(retro3do_pad_button button) {
    return static_cast<PadButton>(button);
}

// The ABI's button order is the machine's button order. If anyone reorders
// either, this stops the build rather than quietly remapping the controls.
static_assert(static_cast<int>(PadButton::Up) == RETRO3DO_PAD_UP, "");
static_assert(static_cast<int>(PadButton::Down) == RETRO3DO_PAD_DOWN, "");
static_assert(static_cast<int>(PadButton::Left) == RETRO3DO_PAD_LEFT, "");
static_assert(static_cast<int>(PadButton::Right) == RETRO3DO_PAD_RIGHT, "");
static_assert(static_cast<int>(PadButton::A) == RETRO3DO_PAD_A, "");
static_assert(static_cast<int>(PadButton::B) == RETRO3DO_PAD_B, "");
static_assert(static_cast<int>(PadButton::C) == RETRO3DO_PAD_C, "");
static_assert(static_cast<int>(PadButton::Play) == RETRO3DO_PAD_PLAY, "");
static_assert(static_cast<int>(PadButton::Stop) == RETRO3DO_PAD_STOP, "");
static_assert(static_cast<int>(PadButton::LeftShift) == RETRO3DO_PAD_LEFT_SHIFT,
              "");
static_assert(static_cast<int>(PadButton::RightShift) ==
                  RETRO3DO_PAD_RIGHT_SHIFT,
              "");
static_assert(static_cast<int>(PadButton::Count) == RETRO3DO_PAD_COUNT, "");
static_assert(kMaxPads == RETRO3DO_MAX_PADS, "");
static_assert(kAudioSampleRate == RETRO3DO_SAMPLE_RATE, "");

}  // namespace

extern "C" {

int retro3do_abi_version(void) { return RETRO3DO_ABI_VERSION; }

// --- lifecycle -------------------------------------------------------------

retro3do_t *retro3do_create(void) {
    // The machine is large and a failure to allocate it is a real possibility
    // on a handheld, so this is one of the few places worth catching.
    try {
        return new retro3do_t();
    } catch (...) {
        return nullptr;
    }
}

void retro3do_destroy(retro3do_t *emu) {
    if (emu == nullptr) {
        return;
    }
    emu->thread.stop();
    delete emu;
}

const char *retro3do_last_error(const retro3do_t *emu) {
    if (emu == nullptr) {
        return "no emulator";
    }
    return emu->error.c_str();
}

// --- media -----------------------------------------------------------------

int retro3do_load_bios(retro3do_t *emu, const char *path) {
    if (emu == nullptr || path == nullptr) {
        return 0;
    }
    if (!emu->console.load_bios(path)) {
        return fail_from_console(emu, "could not load that BIOS image");
    }
    return 1;
}

int retro3do_load_bios_fd(retro3do_t *emu, int fd, const char *display_name) {
    if (emu == nullptr) {
        return 0;
    }
    if (!emu->console.load_bios_fd(fd, display_name ? display_name : "")) {
        return fail_from_console(emu, "could not load that BIOS image");
    }
    return 1;
}

int retro3do_load_demo(retro3do_t *emu) {
    if (emu == nullptr) {
        return 0;
    }
    if (!emu->console.load_builtin_demo()) {
        return fail_from_console(emu, "could not start the built-in demo");
    }
    return 1;
}

int retro3do_load_disc(retro3do_t *emu, const char *path) {
    if (emu == nullptr || path == nullptr) {
        return 0;
    }
    if (!emu->console.load_disc(path)) {
        return fail_from_console(emu, "could not open that disc image");
    }
    return 1;
}

int retro3do_load_disc_fd(retro3do_t *emu, int fd, const char *display_name) {
    if (emu == nullptr) {
        return 0;
    }
    if (!emu->console.load_disc_fd(fd, display_name ? display_name : "")) {
        return fail_from_console(emu, "could not open that disc image");
    }
    return 1;
}

void retro3do_eject_disc(retro3do_t *emu) {
    if (emu != nullptr) {
        emu->console.eject_disc();
    }
}

int retro3do_bios_loaded(const retro3do_t *emu) {
    return emu != nullptr && emu->console.bios_loaded() ? 1 : 0;
}

int retro3do_demo_loaded(const retro3do_t *emu) {
    return emu != nullptr && emu->console.demo_loaded() ? 1 : 0;
}

int retro3do_disc_loaded(const retro3do_t *emu) {
    return emu != nullptr && emu->console.disc_loaded() ? 1 : 0;
}

int retro3do_get_disc_info(const retro3do_t *emu, retro3do_disc_info *out) {
    if (emu == nullptr || out == nullptr || !emu->console.disc_loaded()) {
        return 0;
    }
    const Disc &disc = emu->console.disc();
    out->path = disc.path().c_str();
    out->is_chd = disc.is_chd() ? 1 : 0;
    out->sector_count = disc.sector_count();
    out->track_count = static_cast<uint32_t>(disc.tracks().size());
    return 1;
}

// --- the machine -----------------------------------------------------------

void retro3do_set_region(retro3do_t *emu, retro3do_region region) {
    if (emu != nullptr) {
        emu->console.set_region(region == RETRO3DO_REGION_PAL ? Region::Pal
                                                              : Region::Ntsc);
    }
}

retro3do_region retro3do_get_region(const retro3do_t *emu) {
    if (emu == nullptr || emu->console.region() != Region::Pal) {
        return RETRO3DO_REGION_NTSC;
    }
    return RETRO3DO_REGION_PAL;
}

void retro3do_set_cpu_scale_percent(retro3do_t *emu, uint32_t percent) {
    if (emu != nullptr) {
        emu->console.set_cpu_scale_percent(percent);
    }
}

uint32_t retro3do_get_cpu_scale_percent(const retro3do_t *emu) {
    return emu == nullptr ? 100u : emu->console.cpu_scale_percent();
}

void retro3do_set_render_scale(retro3do_t *emu, uint32_t scale) {
    if (emu != nullptr) {
        emu->console.set_render_scale(scale);
    }
}

uint32_t retro3do_get_render_scale(const retro3do_t *emu) {
    return emu == nullptr ? 1u : emu->console.render_scale();
}

void retro3do_reset(retro3do_t *emu) {
    if (emu == nullptr) {
        return;
    }
    // Resetting the machine from this thread while the emulator is midway
    // through a frame is a data race, so a running machine gets the deferred
    // reset instead of the immediate one. Callers should not have to know which
    // state they are in to reset a console.
    if (emu->thread.running()) {
        emu->thread.request_reset();
    } else {
        emu->console.reset();
    }
}

// --- save states -----------------------------------------------------------

namespace {

// Run `body` with the emulator thread parked, then start it again if it was
// going. A state must be taken between frames; stopping the thread is the
// simplest way to be between frames, and it costs one join.
int with_machine_stopped(retro3do_t *emu, const std::function<int()> &body) {
    const bool was_running = emu->thread.running();
    // Stopping un-pauses the thread so it can notice the stop. A game paused
    // behind a menu must still be paused when it comes back.
    const bool was_paused = emu->thread.paused();
    if (was_running) emu->thread.stop();
    const int result = body();
    if (was_running) {
        emu->thread.set_paused(was_paused);
        emu->thread.start();
    }
    return result;
}

}  // namespace

int retro3do_save_state(retro3do_t *emu, const char *path) {
    if (emu == nullptr || path == nullptr) return 0;
    return with_machine_stopped(emu, [&] {
        const std::vector<u8> state = emu->console.save_state();
        // Written beside the target and renamed over it, so a full disk or a
        // crash mid-write leaves the previous state in the slot, not half of
        // the new one.
        const std::string tmp = std::string(path) + ".tmp";
        std::FILE *f = std::fopen(tmp.c_str(), "wb");
        if (f == nullptr) return fail(emu, "could not write the save state");
        const bool written = std::fwrite(state.data(), 1, state.size(), f) == state.size();
        const bool closed = std::fclose(f) == 0;
        if (!written || !closed || std::rename(tmp.c_str(), path) != 0) {
            std::remove(tmp.c_str());
            return fail(emu, "could not write the save state");
        }
        return 1;
    });
}

int retro3do_load_state(retro3do_t *emu, const char *path) {
    if (emu == nullptr || path == nullptr) return 0;
    std::FILE *f = std::fopen(path, "rb");
    if (f == nullptr) return fail(emu, "there is no save state there");
    std::vector<u8> state;
    u8 chunk[65536];
    size_t got = 0;
    while ((got = std::fread(chunk, 1, sizeof chunk, f)) > 0) {
        state.insert(state.end(), chunk, chunk + got);
    }
    std::fclose(f);
    return with_machine_stopped(emu, [&] {
        if (!emu->console.load_state(state.data(), state.size())) {
            return fail_from_console(emu, "could not load that save state");
        }
        return 1;
    });
}

// --- running ---------------------------------------------------------------

void retro3do_start(retro3do_t *emu) {
    if (emu != nullptr) {
        emu->thread.start();
    }
}

void retro3do_stop(retro3do_t *emu) {
    if (emu != nullptr) {
        emu->thread.stop();
    }
}

int retro3do_running(const retro3do_t *emu) {
    return emu != nullptr && emu->thread.running() ? 1 : 0;
}

void retro3do_set_paused(retro3do_t *emu, int paused) {
    if (emu != nullptr) {
        emu->thread.set_paused(paused != 0);
    }
}

int retro3do_paused(const retro3do_t *emu) {
    return emu != nullptr && emu->thread.paused() ? 1 : 0;
}

void retro3do_request_reset(retro3do_t *emu) {
    if (emu != nullptr) {
        emu->thread.request_reset();
    }
}

uint32_t retro3do_run_frame(retro3do_t *emu) {
    if (emu == nullptr || emu->thread.running()) {
        return 0;
    }
    const u32 cycles = emu->console.run_frame();
    publish_frame(emu);
    emu->thread.call_frame_hook();
    return cycles;
}

// --- video -----------------------------------------------------------------

int retro3do_acquire_frame(retro3do_t *emu, retro3do_frame *out) {
    if (emu == nullptr || out == nullptr) {
        return 0;
    }
    int width = 0, height = 0;
    const u32 *pixels = emu->mailbox.acquire(&width, &height);
    if (pixels == nullptr) {
        return 0;
    }
    out->pixels = pixels;
    out->width = width;
    out->height = height;
    return 1;
}

int retro3do_current_frame(retro3do_t *emu, retro3do_frame *out) {
    if (emu == nullptr || out == nullptr) {
        return 0;
    }
    // The mailbox seeds its slots with opaque black so a consumer can never see
    // uninitialised memory, which means "what is in the mailbox" is not the same
    // question as "has the machine drawn anything". A front end needs the
    // second one — it is the difference between showing a black screen and
    // knowing not to start drawing yet — so this reports on published frames
    // rather than on the buffer being non-null.
    int width = 0, height = 0;
    const u32 *pixels = emu->mailbox.current(&width, &height);
    if (pixels == nullptr || emu->mailbox.published() == 0) {
        return 0;
    }
    out->pixels = pixels;
    out->width = width;
    out->height = height;
    return 1;
}

// --- audio -----------------------------------------------------------------

uint32_t retro3do_audio_available(const retro3do_t *emu) {
    return emu == nullptr ? 0u : mut(emu)->console.audio().available();
}

uint32_t retro3do_audio_pull(retro3do_t *emu, int16_t *interleaved,
                             uint32_t frames) {
    if (emu == nullptr || interleaved == nullptr || frames == 0) {
        return 0;
    }
    // StereoSample is two int16_t in left, right order, which is the layout the
    // caller asked for, so the ring fills the caller's buffer directly rather
    // than going through a staging copy on the audio thread.
    static_assert(sizeof(StereoSample) == 2 * sizeof(int16_t), "");
    return emu->console.audio().pull(
        reinterpret_cast<StereoSample *>(interleaved), frames);
}

uint64_t retro3do_audio_underruns(const retro3do_t *emu) {
    return emu == nullptr ? 0u : mut(emu)->console.audio().underruns();
}

// --- input -----------------------------------------------------------------

void retro3do_pad_connect(retro3do_t *emu, uint32_t pad, int connected) {
    if (emu != nullptr && valid_pad(pad)) {
        emu->console.pads().set_connected(pad, connected != 0);
    }
}

int retro3do_pad_connected(const retro3do_t *emu, uint32_t pad) {
    if (emu == nullptr || !valid_pad(pad)) {
        return 0;
    }
    return mut(emu)->console.pads().connected(pad) ? 1 : 0;
}

uint32_t retro3do_pad_count(const retro3do_t *emu) {
    return emu == nullptr ? 0u : mut(emu)->console.pads().connected_count();
}

void retro3do_pad_press(retro3do_t *emu, uint32_t pad, retro3do_pad_button button,
                        int down) {
    if (emu != nullptr && valid_pad(pad) && button < RETRO3DO_PAD_COUNT) {
        emu->console.pads().press(pad, to_button(button), down != 0);
    }
}

int retro3do_pad_pressed(const retro3do_t *emu, uint32_t pad,
                         retro3do_pad_button button) {
    if (emu == nullptr || !valid_pad(pad) || button >= RETRO3DO_PAD_COUNT) {
        return 0;
    }
    return mut(emu)->console.pads().pressed(pad, to_button(button)) ? 1 : 0;
}

void retro3do_pad_set_buttons(retro3do_t *emu, uint32_t pad, uint32_t buttons) {
    if (emu == nullptr || !valid_pad(pad)) {
        return;
    }
    PadState &pads = emu->console.pads();
    for (int i = 0; i < RETRO3DO_PAD_COUNT; ++i) {
        pads.press(pad, static_cast<PadButton>(i),
                   (buttons & (1u << i)) != 0u);
    }
}

uint32_t retro3do_pad_get_buttons(const retro3do_t *emu, uint32_t pad) {
    if (emu == nullptr || !valid_pad(pad)) {
        return 0;
    }
    return mut(emu)->console.pads().buttons(pad);
}

// --- saved games -----------------------------------------------------------

uint32_t retro3do_nvram_size(const retro3do_t *emu) {
    return emu == nullptr
               ? 0u
               : static_cast<uint32_t>(mut(emu)->console.bus().nvram_size());
}

int retro3do_take_nvram(retro3do_t *emu, uint8_t *out, uint32_t capacity,
                        uint32_t *out_size) {
    if (emu == nullptr || out == nullptr) {
        return 0;
    }
    if (!emu->thread.take_nvram(emu->nvram_scratch)) {
        return 0;
    }
    if (emu->nvram_scratch.size() > capacity) {
        return fail(emu, "NVRAM buffer is too small");
    }
    std::memcpy(out, emu->nvram_scratch.data(), emu->nvram_scratch.size());
    if (out_size != nullptr) {
        *out_size = static_cast<uint32_t>(emu->nvram_scratch.size());
    }
    return 1;
}

int retro3do_restore_nvram(retro3do_t *emu, const uint8_t *data, uint32_t size) {
    if (emu == nullptr || data == nullptr) {
        return 0;
    }
    return with_machine_stopped(emu, [&] {
        if (!emu->console.bus().restore_nvram(data, size)) {
            return fail(emu, "that save data is not a valid NVRAM image");
        }
        emu->thread.discard_nvram();
        return 1;
    });
}

int retro3do_format_nvram(retro3do_t *emu) {
    if (emu == nullptr) {
        return 0;
    }
    return with_machine_stopped(emu, [&] {
        emu->console.bus().format_nvram();
        // A fresh image is not a save worth writing back until a game uses it.
        emu->console.bus().clear_nvram_dirty();
        emu->thread.discard_nvram();
        return 1;
    });
}

// --- diagnostics -----------------------------------------------------------

void retro3do_get_stats(const retro3do_t *emu, retro3do_stats *out) {
    if (out == nullptr) {
        return;
    }
    *out = retro3do_stats{};
    if (emu == nullptr) {
        return;
    }
    const EmulatorStats stats = emu->thread.stats();
    out->frames_emulated = stats.frames_emulated;
    out->frames_behind = stats.frames_behind;
    out->deadline_resets = stats.deadline_resets;
    out->emulated_fps = stats.emulated_fps;
    out->frame_ms = stats.frame_ms;
    out->audio_underruns = mut(emu)->console.audio().underruns();
    out->expansion_dma = emu->console.expansion_dma_count();
    out->cpu_pc = mut(emu)->console.cpu().pc();
}

}  // extern "C"

// --- inspection ------------------------------------------------------------

void retro3do_set_frame_hook(retro3do_t *emu, retro3do_frame_hook hook, void *user) {
    if (emu != nullptr) emu->thread.set_frame_hook(hook, user);
}

uint32_t retro3do_read_ram(const retro3do_t *emu, uint32_t address, uint8_t *out,
                           uint32_t count) {
    if (emu == nullptr || out == nullptr) return 0;
    const u8 *dram = mut(emu)->console.bus().dram();
    uint32_t backed = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const uint64_t at = uint64_t{address} + i;
        if (at < kDramSize) {
            out[i] = dram[at];
            ++backed;
        } else {
            out[i] = 0;
        }
    }
    return backed;
}

int retro3do_read_disc_sectors(retro3do_t *emu, uint32_t lba, uint32_t count,
                               uint8_t *out) {
    if (emu == nullptr || out == nullptr) return 0;
    if (!emu->console.disc_loaded()) return fail(emu, "no disc in the drive");
    return with_machine_stopped(emu, [&] {
        Disc &disc = emu->console.mutable_disc();
        for (uint32_t i = 0; i < count; ++i) {
            if (!disc.read_sector(lba + i, out + size_t{i} * 2048))
                return fail(emu, "could not read that sector of the disc");
        }
        return 1;
    });
}
