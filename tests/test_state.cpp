// Save states: a machine put back from a state must carry on exactly as the
// one it was taken from would have. Anything the state forgot shows up as the
// two runs drifting apart.
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "retro3do.h"
#include "test_harness.h"

namespace {

std::vector<uint32_t> run_and_capture(retro3do_t *emu, int frames) {
    for (int i = 0; i < frames; ++i) retro3do_run_frame(emu);
    retro3do_frame f{};
    std::vector<uint32_t> out;
    if (retro3do_current_frame(emu, &f))
        out.assign(f.pixels, f.pixels + f.width * f.height);
    // The audio produced on the way is part of "the same run" too.
    int16_t pcm[2048 * 2];
    uint32_t n = retro3do_audio_available(emu);
    while (n > 0) {
        const uint32_t take = n > 2048 ? 2048 : n;
        retro3do_audio_pull(emu, pcm, take);
        for (uint32_t i = 0; i < take * 2; ++i) out.push_back(static_cast<uint16_t>(pcm[i]));
        n -= take;
    }
    return out;
}

std::string temp_state(const char *name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

}  // namespace

TEST(a_loaded_state_carries_on_exactly_like_the_original) {
    const std::string path = temp_state("retro3do_state_test.a3s");
    retro3do_t *emu = retro3do_create();
    CHECK(retro3do_load_demo(emu));
    retro3do_reset(emu);
    run_and_capture(emu, 90);
    CHECK(retro3do_save_state(emu, path.c_str()));
    const auto expected = run_and_capture(emu, 120);
    CHECK(!expected.empty());

    // A fresh machine, run somewhere else entirely, then put back.
    retro3do_t *other = retro3do_create();
    CHECK(retro3do_load_demo(other));
    retro3do_reset(other);
    run_and_capture(other, 17);
    CHECK(retro3do_load_state(other, path.c_str()));
    const auto replayed = run_and_capture(other, 120);
    CHECK(replayed == expected);

    // And the original can go back to its own past.
    CHECK(retro3do_load_state(emu, path.c_str()));
    CHECK(run_and_capture(emu, 120) == expected);

    retro3do_destroy(other);
    retro3do_destroy(emu);
    std::remove(path.c_str());
}

TEST(a_damaged_or_foreign_state_is_refused_and_changes_nothing) {
    const std::string path = temp_state("retro3do_state_bad.a3s");
    retro3do_t *emu = retro3do_create();
    CHECK(retro3do_load_demo(emu));
    retro3do_reset(emu);
    run_and_capture(emu, 30);
    CHECK(retro3do_save_state(emu, path.c_str()));

    // Cut in half.
    const auto size = std::filesystem::file_size(path);
    std::filesystem::resize_file(path, size / 2);
    const std::string before_path = temp_state("retro3do_state_before.a3s");
    CHECK(retro3do_save_state(emu, before_path.c_str()));
    CHECK(!retro3do_load_state(emu, path.c_str()));
    CHECK(std::string(retro3do_last_error(emu)).find("could not be loaded") != std::string::npos);

    // Still the same machine: it runs on as it would have.
    retro3do_t *twin = retro3do_create();
    CHECK(retro3do_load_demo(twin));
    CHECK(retro3do_load_state(twin, before_path.c_str()));
    CHECK(run_and_capture(emu, 30) == run_and_capture(twin, 30));

    // Not a state at all.
    { std::FILE *f = std::fopen(path.c_str(), "wb"); std::fputs("hello", f); std::fclose(f); }
    CHECK(!retro3do_load_state(emu, path.c_str()));
    CHECK(!retro3do_load_state(emu, temp_state("retro3do_no_such_state.a3s").c_str()));

    retro3do_destroy(twin);
    retro3do_destroy(emu);
    std::remove(path.c_str());
    std::remove(before_path.c_str());
}

TEST(saving_a_running_paused_game_leaves_it_paused_and_running) {
    const std::string path = temp_state("retro3do_state_paused.a3s");
    retro3do_t *emu = retro3do_create();
    CHECK(retro3do_load_demo(emu));
    retro3do_reset(emu);
    retro3do_start(emu);
    retro3do_set_paused(emu, 1);
    CHECK(retro3do_save_state(emu, path.c_str()));
    CHECK(retro3do_running(emu));
    CHECK(retro3do_paused(emu));
    CHECK(retro3do_load_state(emu, path.c_str()));
    CHECK(retro3do_running(emu));
    CHECK(retro3do_paused(emu));
    retro3do_set_paused(emu, 0);
    CHECK(retro3do_save_state(emu, path.c_str()));
    CHECK(!retro3do_paused(emu));
    retro3do_stop(emu);
    retro3do_destroy(emu);
    std::remove(path.c_str());
}
