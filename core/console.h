// The machine as a whole: the parts, and the frame loop that drives them.
//
// The console never presents. It emulates a frame into `framebuffer()` and
// returns; whoever owns the window decides when to draw it. That separation is
// the whole reason this rewrite exists — the previous core called into the GPU
// from inside its emulation thread, so a slow present stalled emulation and the
// machine appeared to run slow when it was really being throttled.
#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "arm60.h"
#include "bus.h"
#include "audio_ring.h"
#include "disc.h"
#include "clio.h"
#include "hires.h"
#include "madam.h"
#include "dsp.h"
#include "pbus.h"
#include "pad.h"
#include "sport.h"
#include "vdlp.h"
#include "types.h"

namespace retro3do {

enum class Region {
    Ntsc,   // 320x240, 60 fields per second
    Pal,    // 320x288, 50 fields per second
};

// A frame handed to the front end. Pixels are XRGB8888 in host order, ready to
// upload without further conversion.
struct Frame {
    const u32* pixels = nullptr;
    int width  = 0;
    int height = 0;
};

class Console : public DspHost {
public:
    Console();
    ~Console();

    Console(const Console&) = delete;
    Console& operator=(const Console&) = delete;

    // Load a BIOS image from disk. Without one the machine has nothing to run.
    bool load_bios(const std::string& path);
    // Load the project-owned demonstration ROM embedded in the app. It is a
    // hardware test, not a replacement BIOS and cannot boot commercial discs.
    bool load_builtin_demo();
    bool bios_loaded() const { return bus_.bios_loaded(); }
    bool demo_loaded() const { return demo_loaded_; }

    // Insert a disc image. The disc is opened and its table of contents read;
    // nothing is executed until the machine asks for a sector.
    bool load_disc(const std::string& path);

    // Insert a disc from an already-open descriptor, taking ownership of it.
    // Android's scoped storage hands out descriptors rather than paths;
    // `display_name` supplies the filename the descriptor does not carry.
    bool load_disc_fd(int fd, const std::string& display_name);

    // Load a BIOS from a descriptor, same reason.
    bool load_bios_fd(int fd, const std::string& display_name);
    void eject_disc();
    bool disc_loaded() const { return disc_.is_open(); }
    const Disc& disc() const { return disc_; }

    void set_region(Region region);
    Region region() const { return region_; }

    // Give the ARM more work per real video field without advancing any of the
    // machine's other clocks. This is an optional slowdown-reduction aid, not
    // fast-forward: CLIO, the DSP, CD timing and the 50/60 Hz field cadence all
    // remain at hardware speed. Values outside the supported range are
    // clamped so a damaged settings file cannot create an unusable machine.
    void set_cpu_scale_percent(u32 percent) {
        if (percent < 100u) percent = 100u;
        if (percent > 150u) percent = 150u;
        cpu_scale_percent_.store(percent, std::memory_order_release);
    }
    u32 cpu_scale_percent() const {
        return cpu_scale_percent_.load(std::memory_order_acquire);
    }

    // Draw the CEL engine's output at `scale` times the logical resolution
    // (1 to 6). 1 is the hardware picture: 320 lines widened to 640 by the
    // display generator. Anything above draws every cel onto a finer grid as
    // well, and the field comes out at 320 * scale wide. Takes effect at the
    // start of the next frame, on the emulator's own thread.
    void set_render_scale(u32 scale) {
        if (scale < 1u) scale = 1u;
        if (scale > HiresStore::kMaxScale) scale = HiresStore::kMaxScale;
        render_scale_request_.store(scale, std::memory_order_release);
    }
    u32 render_scale() const {
        return render_scale_request_.load(std::memory_order_acquire);
    }

    void reset();

    // The whole machine as a save state, and back. Call between frames, on
    // the thread that runs them. A state is refused (false, reason in
    // last_error) if it is damaged, from another version, or was made with a
    // different BIOS or disc than the ones loaded now.
    std::vector<u8> save_state();
    bool load_state(const u8* data, size_t size);

    // Emulate one video frame. Returns the number of CPU cycles spent.
    u32 run_frame();

    // How many expansion-bus DMA transfers have been served. A bring-up
    // diagnostic: "is the disc actually being read" is otherwise invisible.
    u64 expansion_dma_count() const { return expansion_dma_count_; }

private:
    void service_expansion_dma();
    void service_pbus_dma();
    Joypad joypad_for(u32 slot) const;
    void tick_dsp(u32 cycles);

public:
    // --- DspHost ----------------------------------------------------------
    // The DSP's DMA channels are MADAM's registers and its interrupt is
    // CLIO's, so the machine wires them together rather than the DSP reaching
    // for either.
    u16  dsp_input_next(u16 channel) override { return madam_.fifo_input_next(channel); }
    u16  dsp_input_peek(u16 channel) override { return madam_.fifo_input_peek(channel); }
    u16  dsp_input_status(u16 channel) override { return madam_.fifo_input_status(channel); }
    u16  dsp_output_status(u16 channel) override { return madam_.fifo_output_status(channel); }
    void dsp_output(u16 channel, u16 value) override { madam_.fifo_output(channel, value); }
    void dsp_audio_interrupt() override { clio_.raise(kIrqAudioTimer); }

    const Dsp& dsp() const { return dsp_; }

private:

public:
    // The controller state the machine will report on its next input scan.
    // Set pad one directly. Kept for the headless harness, which has no
    // PadState of its own to drive; it writes through to the same place the
    // front end does so that there is only ever one answer to "what is held".
    void set_joypad(const Joypad& pad);
    Joypad joypad() const { return joypad_for(0); }

private:
    u64 expansion_dma_count_ = 0;

public:

    Frame framebuffer() const;

    Arm60& cpu() { return cpu_; }
    Bus& bus() { return bus_; }
    Clio& clio() { return clio_; }
    Vdlp& vdlp() { return vdlp_; }
    Madam& madam() { return madam_; }
    Sport& sport() { return sport_; }
    PadState& pads() { return pads_; }
    AudioRing& audio() { return audio_; }

    const std::string& last_error() const { return last_error_; }

private:
    void apply_write_watch();

    Bus   bus_;
    Arm60 cpu_;
    Clio  clio_;
    Vdlp  vdlp_;
    Madam madam_;
    Dsp dsp_;
    Pbus pbus_;
    // The DSP runs one pass of its program per audio sample. At 44.1 kHz
    // that is about every 283 CPU cycles.
    static constexpr u32 kSamplePeriod = 12500000u / 44100u;
    u32 sample_accumulator_ = 0;

    // The last pair of DAC words the DSP produced, as (right << 16) | left.
    u32 last_sample_ = 0;

    // Samples collected during a frame, handed to the ring at the end of it.
    // A frame's worth at 44.1 kHz is under a thousand pairs; the cap is only
    // there so a runaway sample clock cannot grow this without bound.
    static constexpr size_t kMaxSamplesPerFrame = 4096;
    std::vector<StereoSample> samples_;
    Sport sport_;
    Disc  disc_;
    PadState  pads_;
    AudioRing audio_;

    Region region_ = Region::Ntsc;
    int frame_width_  = 320;
    int frame_height_ = 240;

    std::vector<u32> framebuffer_;
    std::string last_error_;
    bool demo_loaded_ = false;

    u64 frame_count_ = 0;
    std::atomic<u32> cpu_scale_percent_{100};

    HiresStore hires_;
    u32 render_scale_ = 1;
    std::atomic<u32> render_scale_request_{1};
    void apply_render_scale(u32 scale);
    int output_scale() const { return render_scale_ > 1 ? static_cast<int>(render_scale_) : 2; }
    void size_framebuffer();
};

}  // namespace retro3do
