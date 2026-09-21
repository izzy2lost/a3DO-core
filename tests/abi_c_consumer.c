/*
 * An application, reduced to the smallest one that can exist.
 *
 * This is a C file, not C++, and it is here to prove two things the C++ tests
 * cannot:
 *
 *   1. retro3do.h is really plain C. The core is C++20 and the header is
 *      written by people working in C++ all day, so a stray `bool`, a default
 *      argument or a `//`-only construct would go unnoticed for months and then
 *      surface as an unreadable Swift import error. Swift's module map feeds
 *      this header to a C compiler; so does this.
 *
 *   2. The header is self-sufficient. It is compiled with ONLY the core's
 *      public include directory on the path, so if it ever quietly depended on
 *      something under core/ this stops building.
 *
 * It exercises the interface rather than the machine - the machine has its own
 * tests. What matters here is that every declaration is reachable, callable and
 * correctly typed from C.
 */
#include <stdio.h>

#include "retro3do.h"

int main(void) {
    if (retro3do_abi_version() != RETRO3DO_ABI_VERSION) {
        printf("ABI version mismatch: library %d, header %d\n",
               retro3do_abi_version(), RETRO3DO_ABI_VERSION);
        return 1;
    }

    retro3do_t *emu = retro3do_create();
    if (emu == NULL) {
        printf("could not create a machine\n");
        return 1;
    }

    if (!retro3do_load_demo(emu)) {
        printf("could not load the demonstration ROM: %s\n",
               retro3do_last_error(emu));
        retro3do_destroy(emu);
        return 1;
    }

    retro3do_set_region(emu, RETRO3DO_REGION_NTSC);
    retro3do_set_cpu_scale_percent(emu, 100);
    retro3do_pad_connect(emu, 0, 1);
    retro3do_pad_press(emu, 0, RETRO3DO_PAD_A, 1);

    if (retro3do_run_frame(emu) == 0) {
        printf("the machine ran no cycles\n");
        retro3do_destroy(emu);
        return 1;
    }

    retro3do_frame frame;
    if (!retro3do_acquire_frame(emu, &frame) || frame.pixels == NULL) {
        printf("no frame arrived\n");
        retro3do_destroy(emu);
        return 1;
    }

    short samples[64];
    retro3do_audio_pull(emu, samples, 32);

    retro3do_stats stats;
    retro3do_get_stats(emu, &stats);

    retro3do_destroy(emu);

    printf("ok: %dx%d frame, ABI %d\n", frame.width, frame.height,
           RETRO3DO_ABI_VERSION);
    return 0;
}
