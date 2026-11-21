/*
 * Retro3DO Core — the public interface.
 *
 * This is the whole of it. Everything under core/ is an implementation detail
 * and no application should include any of it: the machine is C++ and moves
 * about as hardware understanding improves, whereas this header is a contract
 * and changes only with its version number.
 *
 * It is plain C on purpose. Two applications wrap this core — an Android and
 * Linux front end written in C++ against SDL, and an iOS front end written in
 * Swift — and a C ABI is the only surface both can consume without one of them
 * carrying a language bridge it has no other use for. Swift imports this header
 * directly through a module map; there is nothing to generate and nothing to
 * keep in step by hand.
 *
 * What the core owns
 * ------------------
 * The machine, the thread it runs on, and the pacing policy. That last one is
 * deliberate and is the reason the runner is in here rather than in each
 * application: the previous 3DO core stacked three independent frame-droppers
 * on top of each other, none aware of the others, and the result was a machine
 * that reported itself healthy while dropping most of its frames. There is one
 * policy, it lives in core/emulator_thread.h, and an application cannot
 * accidentally invent a second one because it has no way to reach the parts
 * that would let it.
 *
 * What the application owns
 * -------------------------
 * The window, the renderer, the audio device, where files live and what the
 * user is shown. The core never presents, never opens an audio device and has
 * no opinion about layout.
 *
 * Threading
 * ---------
 * A handle may be created and destroyed on any one thread. Once started, the
 * emulator runs on its own thread inside the core, and these are safe to call
 * from another thread while it does:
 *
 *   retro3do_acquire_frame / retro3do_current_frame   (one consumer)
 *   retro3do_audio_pull / retro3do_audio_available    (one consumer)
 *   the pad functions, the stats function, retro3do_set_paused,
 *   retro3do_request_reset, retro3do_set_cpu_scale_percent,
 *   retro3do_set_render_scale
 *
 * Everything else — loading media, changing region, destroy — expects the
 * emulator to be stopped or paused; they reach into machine state that a
 * running frame is midway through.
 */
#ifndef RETRO3DO_H
#define RETRO3DO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bumped when anything already declared here changes meaning or shape. Adding
 * a new function does not bump it; an application built against version N keeps
 * working against a core reporting N. */
#define RETRO3DO_ABI_VERSION 1

/* The version the core was built as, which is not necessarily the version the
 * header in front of you declares — check them against each other when the core
 * arrives as a prebuilt library. */
int retro3do_abi_version(void);

/* An emulated machine. Opaque: its size and contents are C++ and are not part
 * of this interface. */
typedef struct retro3do_machine retro3do_t;

/* ------------------------------------------------------------------------ */
/* Lifecycle                                                                 */
/* ------------------------------------------------------------------------ */

/* A powered-off machine with no media. Returns NULL only if allocation fails. */
retro3do_t *retro3do_create(void);

/* Stops the emulator thread if it is running, then frees everything. Safe on
 * NULL. */
void retro3do_destroy(retro3do_t *emu);

/* The failure message from the last call that returned 0. Owned by the core and
 * valid until the next failing call. Never NULL. */
const char *retro3do_last_error(const retro3do_t *emu);

/* ------------------------------------------------------------------------ */
/* Media                                                                     */
/* ------------------------------------------------------------------------ */
/*
 * All of these return 1 on success and 0 on failure, with the reason in
 * retro3do_last_error.
 *
 * The _fd variants exist because Android's Storage Access Framework hands out
 * file descriptors rather than paths — a content:// URI cannot be fopen'd — and
 * because an iOS security-scoped bookmark resolves to something equally
 * unsuitable for holding on to. The core takes ownership of the descriptor and
 * closes it. `display_name` is the filename the descriptor does not carry; it
 * is used for the disc's reported name and to pick a container format, so it
 * must keep its extension.
 */
int retro3do_load_bios(retro3do_t *emu, const char *path);
int retro3do_load_bios_fd(retro3do_t *emu, int fd, const char *display_name);

/* The project's own hardware demonstration ROM, built into the core. It is a
 * hardware test rather than a BIOS: it cannot boot a commercial disc, and it is
 * here so the machine has something legal to run before the user has found a
 * BIOS image of their own. */
int retro3do_load_demo(retro3do_t *emu);

int retro3do_load_disc(retro3do_t *emu, const char *path);
int retro3do_load_disc_fd(retro3do_t *emu, int fd, const char *display_name);
void retro3do_eject_disc(retro3do_t *emu);

int retro3do_bios_loaded(const retro3do_t *emu);
int retro3do_demo_loaded(const retro3do_t *emu);
int retro3do_disc_loaded(const retro3do_t *emu);

typedef struct {
    /* The path or display name the disc was opened with. Owned by the core,
     * valid until the disc is ejected or replaced. */
    const char *path;
    /* CHD rather than ISO/BIN/CUE. Worth surfacing: nearly every 3DO dump in
     * circulation is a CHD and being able to say so in the UI turns "it did not
     * load" into a much shorter conversation. */
    int is_chd;
    uint32_t sector_count;
    uint32_t track_count;
} retro3do_disc_info;

/* Fills `out` and returns 1, or returns 0 when no disc is loaded. */
int retro3do_get_disc_info(const retro3do_t *emu, retro3do_disc_info *out);

/* ------------------------------------------------------------------------ */
/* The machine                                                               */
/* ------------------------------------------------------------------------ */

typedef enum {
    RETRO3DO_REGION_NTSC = 0, /* 60 fields a second */
    RETRO3DO_REGION_PAL  = 1  /* 50 fields a second, taller picture */
} retro3do_region;

void retro3do_set_region(retro3do_t *emu, retro3do_region region);
retro3do_region retro3do_get_region(const retro3do_t *emu);

/*
 * Give the ARM more work per video field without moving any of the machine's
 * other clocks. This is a slowdown-reduction aid for titles that were written
 * against a faster machine than they got, not a fast-forward: CLIO, the DSP, CD
 * timing and the field cadence all stay at hardware speed. Clamped to
 * 100..150, so a damaged settings file cannot produce an unusable machine.
 */
void retro3do_set_cpu_scale_percent(retro3do_t *emu, uint32_t percent);
uint32_t retro3do_get_cpu_scale_percent(const retro3do_t *emu);

/*
 * High-resolution rendering. 1 (the default) is the hardware picture: a
 * 320-wide framebuffer that the display generator widens to 640. 2 to 6 also
 * draw every cel onto a grid that many times finer, so polygons and scaled
 * sprites keep their true sub-pixel edges, and frames come out at 320 * scale
 * wide (240 or 288 * scale tall). Pixels the CPU draws itself, full-motion
 * video for instance, stay at the hardware resolution. Values outside 1-6 are
 * clamped. Safe to call at any time from any thread; it takes effect at the
 * next frame, and frame width and height report the new size from then on.
 */
#define RETRO3DO_MAX_RENDER_SCALE 6
void retro3do_set_render_scale(retro3do_t *emu, uint32_t scale);
uint32_t retro3do_get_render_scale(const retro3do_t *emu);

/* Reset the machine. A stopped machine resets immediately; a running one resets
 * at its next frame boundary, because resetting it from another thread midway
 * through a frame is a data race. A caller should not have to know which state
 * it is in to reset a console. */
void retro3do_reset(retro3do_t *emu);

/* ------------------------------------------------------------------------ */
/* Running                                                                   */
/* ------------------------------------------------------------------------ */

/* Start the emulator thread. Does nothing if it is already running. */
void retro3do_start(retro3do_t *emu);

/* Stop it and join. Machine state survives, so a start after this resumes
 * exactly where it left off. */
void retro3do_stop(retro3do_t *emu);
int retro3do_running(const retro3do_t *emu);

/* Pause without tearing the thread down: state survives and resuming is
 * instant. The thread idles rather than spinning. */
void retro3do_set_paused(retro3do_t *emu, int paused);
int retro3do_paused(const retro3do_t *emu);

/* Reset at the next frame boundary rather than from the calling thread, which
 * would otherwise be reaching into machine state midway through a frame. */
void retro3do_request_reset(retro3do_t *emu);

/*
 * Emulate exactly one field on the calling thread and return the CPU cycles it
 * cost. For headless harnesses and tests; it is an error to call this while the
 * emulator thread is running and it will return 0.
 *
 * A harness that instead steps the CPU directly never ticks CLIO, so every poll
 * of a video counter waits forever and the machine looks broken when the
 * harness is. Drive whole frames.
 */
uint32_t retro3do_run_frame(retro3do_t *emu);

/* ------------------------------------------------------------------------ */
/* Save states                                                               */
/* ------------------------------------------------------------------------ */

/*
 * Write the whole machine to `path`, or put it back from one. Safe to call
 * from any thread, running or not: a running emulator is parked between frames
 * for the moment it takes and then carries on.
 *
 * A state belongs to the BIOS and disc it was made with. Loading one with a
 * different disc or BIOS in the machine, or one from another version of the
 * core, fails with the reason in retro3do_last_error and changes nothing.
 * Return 1 on success, 0 on failure.
 */
int retro3do_save_state(retro3do_t *emu, const char *path);
int retro3do_load_state(retro3do_t *emu, const char *path);

/* ------------------------------------------------------------------------ */
/* Video                                                                     */
/* ------------------------------------------------------------------------ */

typedef struct {
    /* XRGB8888 in host order, tightly packed, ready to upload without
     * conversion. Owned by the core; valid until the next acquire. */
    const uint32_t *pixels;
    int width;
    int height;
} retro3do_frame;

/*
 * The newest finished frame, if one has arrived since the last call. Returns 1
 * and fills `out` when there is a new frame, 0 when there is not — which is the
 * signal to skip re-uploading a texture that has not changed.
 *
 * This never blocks and never waits on the emulator. A display that falls
 * behind skips to the newest frame rather than working through a backlog: a
 * stale frame is worthless, and a queue here is latency that never recovers.
 */
int retro3do_acquire_frame(retro3do_t *emu, retro3do_frame *out);

/* The most recent frame whether or not it is new, for when something must be
 * drawn and nothing has arrived — a window resize, or the first frame after a
 * pause. Returns 0 if the machine has never produced one. */
int retro3do_current_frame(retro3do_t *emu, retro3do_frame *out);

/* ------------------------------------------------------------------------ */
/* Audio                                                                     */
/* ------------------------------------------------------------------------ */

#define RETRO3DO_SAMPLE_RATE 44100

/* Stereo frames waiting to be pulled. */
uint32_t retro3do_audio_available(const retro3do_t *emu);

/*
 * Pull `frames` stereo pairs into `interleaved` as left, right, left, right.
 * It always fills the whole request: any shortfall is silence, and the return
 * value is how many pairs were real, so an underrun is distinguishable from a
 * healthy pull.
 *
 * Silence rather than blocking or repeating is deliberate. Blocking would stall
 * the audio thread, which on Android and iOS is the device's audio pipeline and
 * not merely this app's; repeating the last sample produces a buzz that sounds
 * like a broken emulator rather than a dropped frame.
 */
uint32_t retro3do_audio_pull(retro3do_t *emu, int16_t *interleaved,
                             uint32_t frames);

/* A rising underrun count is the first symptom of the machine falling behind,
 * and it appears well before anything is visible on screen. */
uint64_t retro3do_audio_underruns(const retro3do_t *emu);

/* ------------------------------------------------------------------------ */
/* Input                                                                     */
/* ------------------------------------------------------------------------ */
/*
 * 3DO pads daisy-chain: the machine sees one serial stream with every attached
 * pad in it, which is why the console has no multitap. Up to eight.
 */

#define RETRO3DO_MAX_PADS 8

typedef enum {
    RETRO3DO_PAD_UP = 0,
    RETRO3DO_PAD_DOWN,
    RETRO3DO_PAD_LEFT,
    RETRO3DO_PAD_RIGHT,
    RETRO3DO_PAD_A,
    RETRO3DO_PAD_B,
    RETRO3DO_PAD_C,
    RETRO3DO_PAD_PLAY,  /* the "P" button */
    RETRO3DO_PAD_STOP,  /* the "X" button */
    RETRO3DO_PAD_LEFT_SHIFT,
    RETRO3DO_PAD_RIGHT_SHIFT,
    RETRO3DO_PAD_COUNT
} retro3do_pad_button;

void retro3do_pad_connect(retro3do_t *emu, uint32_t pad, int connected);
int retro3do_pad_connected(const retro3do_t *emu, uint32_t pad);
uint32_t retro3do_pad_count(const retro3do_t *emu);

void retro3do_pad_press(retro3do_t *emu, uint32_t pad, retro3do_pad_button button,
                        int down);
int retro3do_pad_pressed(const retro3do_t *emu, uint32_t pad,
                         retro3do_pad_button button);

/* The whole pad at once as a bitmask indexed by retro3do_pad_button. Setting a
 * pad in one call is what stops a diagonal registering as a stutter: the
 * emulator never sees a pad halfway through an update. */
void retro3do_pad_set_buttons(retro3do_t *emu, uint32_t pad, uint32_t buttons);
uint32_t retro3do_pad_get_buttons(const retro3do_t *emu, uint32_t pad);

/* ------------------------------------------------------------------------ */
/* Saved games                                                               */
/* ------------------------------------------------------------------------ */
/*
 * The 3DO keeps saves in battery-backed NVRAM on the board, so persisting them
 * is the application's job: the core has no idea where files belong on the
 * platform it is running on.
 */

uint32_t retro3do_nvram_size(const retro3do_t *emu);

/*
 * Copy out the NVRAM if the machine has written to it since the last time
 * anyone asked. Returns 1 and sets *out_size when there is something new, 0
 * when there is not — which is the usual answer, because titles touch it when
 * the user saves and at no other time, so this is cheap to poll every frame.
 *
 * The copy is taken at a frame boundary by the emulator thread, so what comes
 * out is always a whole consistent image rather than memory caught mid-write.
 * Returns 0 if `capacity` is smaller than retro3do_nvram_size.
 */
int retro3do_take_nvram(retro3do_t *emu, uint8_t *out, uint32_t capacity,
                        uint32_t *out_size);

/* Put a saved image back. Do this before starting, or while paused. */
int retro3do_restore_nvram(retro3do_t *emu, const uint8_t *data, uint32_t size);

/* ------------------------------------------------------------------------ */
/* Diagnostics                                                               */
/* ------------------------------------------------------------------------ */

typedef struct {
    uint64_t frames_emulated;
    /* Fields where the machine missed its deadline, and times it fell so far
     * behind that the clock was restarted rather than accumulating debt it
     * could never pay off. */
    uint64_t frames_behind;
    uint64_t deadline_resets;
    /* What the machine is actually managing, against 60 or 50. This is reported
     * separately from the display's frame rate on purpose: a machine at half
     * speed behind a perfectly smooth window is exactly the failure the old
     * core made invisible. */
    double emulated_fps;
    double frame_ms;
    uint64_t audio_underruns;
    /* Expansion-bus DMA transfers served. A bring-up diagnostic: whether the
     * disc is being read at all is otherwise impossible to see. */
    uint64_t expansion_dma;
    /* Where the ARM is. Read live from another thread, so it is a sample rather
     * than a synchronised value — useful to watch, not to assert on. */
    uint32_t cpu_pc;
} retro3do_stats;

void retro3do_get_stats(const retro3do_t *emu, retro3do_stats *out);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif  /* RETRO3DO_H */
