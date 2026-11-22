// The public interface, exercised the way an application sees it.
//
// This file includes retro3do.h and nothing else from the core on purpose. Every
// other test here reaches into the machine, which is what unit tests of a
// machine should do; this one is the boundary's own test, and if it ever needs a
// core header to say what it means then the boundary has a hole in it.
#include <chrono>
#include <thread>
#include <vector>

#include "retro3do.h"
#include "test_harness.h"

namespace {

// A machine with the built-in demonstration ROM in it, which is the only thing
// the core can run without the user supplying a BIOS image.
struct Machine {
    retro3do_t *emu = nullptr;

    Machine() {
        emu = retro3do_create();
        if (emu != nullptr) {
            retro3do_load_demo(emu);
        }
    }
    ~Machine() { retro3do_destroy(emu); }

    Machine(const Machine &) = delete;
    Machine &operator=(const Machine &) = delete;
};

// Wait for a condition the emulator thread is expected to reach, giving up
// rather than hanging a test run. Sleeps rather than spins: a test that pegs a
// core while waiting is a test that changes the thing it is timing.
template <typename Fn>
bool wait_until(Fn condition, int milliseconds = 2000) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    while (std::chrono::steady_clock::now() < deadline) {
        if (condition()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return condition();
}

}  // namespace

TEST(the_built_library_reports_the_abi_version_its_header_declares) {
    // These come apart when a prebuilt core is older than the header beside it,
    // which is the failure this pair exists to make loud.
    CHECK_EQ(retro3do_abi_version(), RETRO3DO_ABI_VERSION);
}

TEST(every_entry_point_tolerates_a_null_machine) {
    // An application that failed to create a machine should be able to run its
    // teardown path without checking every call, so none of these may crash.
    retro3do_destroy(nullptr);
    retro3do_stop(nullptr);
    retro3do_eject_disc(nullptr);
    retro3do_set_paused(nullptr, 1);
    retro3do_request_reset(nullptr);
    retro3do_reset(nullptr);
    CHECK(retro3do_last_error(nullptr) != nullptr);
    CHECK_EQ(retro3do_bios_loaded(nullptr), 0);
    CHECK_EQ(retro3do_running(nullptr), 0);
    CHECK_EQ(retro3do_run_frame(nullptr), 0u);
    CHECK_EQ(retro3do_audio_available(nullptr), 0u);
    CHECK_EQ(retro3do_pad_get_buttons(nullptr, 0), 0u);

    retro3do_stats stats;
    retro3do_get_stats(nullptr, &stats);
    CHECK_EQ(stats.frames_emulated, 0u);
}

TEST(a_new_machine_has_no_media_and_no_error_to_report) {
    Machine machine;
    CHECK(machine.emu != nullptr);
    CHECK_EQ(retro3do_disc_loaded(machine.emu), 0);
    CHECK_EQ(retro3do_demo_loaded(machine.emu), 1);

    retro3do_disc_info info;
    CHECK_EQ(retro3do_get_disc_info(machine.emu, &info), 0);
}

TEST(a_load_that_fails_says_why_in_words) {
    Machine machine;
    CHECK_EQ(retro3do_load_disc(machine.emu, "/nonexistent/not-a-disc.iso"), 0);
    // The point is that the message is usable in a UI, so an empty string is a
    // failure even though the call correctly returned 0.
    const char *error = retro3do_last_error(machine.emu);
    CHECK(error != nullptr);
    CHECK(error[0] != '\0');
}

TEST(running_a_frame_by_hand_produces_a_frame_to_draw) {
    Machine machine;

    retro3do_frame frame;
    CHECK_EQ(retro3do_current_frame(machine.emu, &frame), 0);

    CHECK(retro3do_run_frame(machine.emu) > 0u);

    CHECK_EQ(retro3do_acquire_frame(machine.emu, &frame), 1);
    CHECK(frame.pixels != nullptr);
    CHECK_EQ(frame.width, 640);
    CHECK_EQ(frame.height, 480);

    // Nothing new has been published since, so a display is told to keep the
    // texture it has rather than re-uploading an identical one.
    CHECK_EQ(retro3do_acquire_frame(machine.emu, &frame), 0);

    // But the last frame is still there for whoever must draw something now.
    CHECK_EQ(retro3do_current_frame(machine.emu, &frame), 1);
    CHECK(frame.pixels != nullptr);
}

TEST(a_pal_machine_produces_a_taller_picture) {
    Machine machine;
    retro3do_set_region(machine.emu, RETRO3DO_REGION_PAL);
    CHECK_EQ(retro3do_get_region(machine.emu), RETRO3DO_REGION_PAL);

    retro3do_run_frame(machine.emu);

    retro3do_frame frame;
    CHECK_EQ(retro3do_acquire_frame(machine.emu, &frame), 1);
    CHECK_EQ(frame.width, 640);
    CHECK_EQ(frame.height, 576);
}

TEST(the_emulator_thread_runs_frames_and_stops_cleanly) {
    Machine machine;
    CHECK_EQ(retro3do_running(machine.emu), 0);

    retro3do_start(machine.emu);
    CHECK_EQ(retro3do_running(machine.emu), 1);

    retro3do_stats stats;
    CHECK(wait_until([&] {
        retro3do_get_stats(machine.emu, &stats);
        return stats.frames_emulated > 0;
    }));

    retro3do_stop(machine.emu);
    CHECK_EQ(retro3do_running(machine.emu), 0);

    // Stopping keeps the machine: what it emulated is still counted, and a
    // start would carry on rather than beginning again.
    retro3do_get_stats(machine.emu, &stats);
    CHECK(stats.frames_emulated > 0);
}

TEST(a_paused_machine_stops_emulating_and_resumes_where_it_was) {
    Machine machine;
    retro3do_start(machine.emu);

    retro3do_stats before;
    CHECK(wait_until([&] {
        retro3do_get_stats(machine.emu, &before);
        return before.frames_emulated > 0;
    }));

    retro3do_set_paused(machine.emu, 1);
    CHECK_EQ(retro3do_paused(machine.emu), 1);
    // Let any frame already in flight finish before taking the reading the
    // pause is being judged by.
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    retro3do_get_stats(machine.emu, &before);

    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    retro3do_stats after;
    retro3do_get_stats(machine.emu, &after);
    CHECK_EQ(after.frames_emulated, before.frames_emulated);

    retro3do_set_paused(machine.emu, 0);
    CHECK(wait_until([&] {
        retro3do_get_stats(machine.emu, &after);
        return after.frames_emulated > before.frames_emulated;
    }));

    retro3do_stop(machine.emu);
}

TEST(running_a_frame_by_hand_is_refused_while_the_thread_owns_the_machine) {
    // Two threads in run_frame at once is the one way an application could
    // corrupt the machine through this interface, so it is refused rather than
    // documented.
    Machine machine;
    retro3do_start(machine.emu);
    CHECK_EQ(retro3do_run_frame(machine.emu), 0u);
    retro3do_stop(machine.emu);
    CHECK(retro3do_run_frame(machine.emu) > 0u);
}

TEST(a_pad_reports_back_exactly_what_was_pressed) {
    Machine machine;

    retro3do_pad_press(machine.emu, 0, RETRO3DO_PAD_A, 1);
    retro3do_pad_press(machine.emu, 0, RETRO3DO_PAD_LEFT, 1);
    CHECK_EQ(retro3do_pad_pressed(machine.emu, 0, RETRO3DO_PAD_A), 1);
    CHECK_EQ(retro3do_pad_pressed(machine.emu, 0, RETRO3DO_PAD_B), 0);
    CHECK_EQ(retro3do_pad_get_buttons(machine.emu, 0),
             (1u << RETRO3DO_PAD_A) | (1u << RETRO3DO_PAD_LEFT));

    retro3do_pad_press(machine.emu, 0, RETRO3DO_PAD_A, 0);
    CHECK_EQ(retro3do_pad_pressed(machine.emu, 0, RETRO3DO_PAD_A), 0);

    // The whole pad at once, which is how a front end reading a host controller
    // should do it: a diagonal must never be seen half applied.
    const uint32_t diagonal =
        (1u << RETRO3DO_PAD_UP) | (1u << RETRO3DO_PAD_RIGHT);
    retro3do_pad_set_buttons(machine.emu, 0, diagonal);
    CHECK_EQ(retro3do_pad_get_buttons(machine.emu, 0), diagonal);

    // Setting the mask clears what it does not name.
    retro3do_pad_set_buttons(machine.emu, 0, 0);
    CHECK_EQ(retro3do_pad_get_buttons(machine.emu, 0), 0u);
}

TEST(a_pad_index_off_the_end_of_the_chain_is_ignored_not_written) {
    // Eight pads daisy-chain; a ninth is a front-end bug and must not become a
    // memory bug.
    Machine machine;
    retro3do_pad_press(machine.emu, RETRO3DO_MAX_PADS, RETRO3DO_PAD_A, 1);
    retro3do_pad_set_buttons(machine.emu, RETRO3DO_MAX_PADS + 100, 0xFFFFFFFFu);
    CHECK_EQ(retro3do_pad_pressed(machine.emu, RETRO3DO_MAX_PADS,
                                  RETRO3DO_PAD_A),
             0);
    CHECK_EQ(retro3do_pad_get_buttons(machine.emu, 0), 0u);
}

TEST(one_pad_is_attached_as_a_machine_ships) {
    Machine machine;
    CHECK_EQ(retro3do_pad_connected(machine.emu, 0), 1);
    CHECK_EQ(retro3do_pad_count(machine.emu), 1u);

    retro3do_pad_connect(machine.emu, 1, 1);
    CHECK_EQ(retro3do_pad_count(machine.emu), 2u);
    retro3do_pad_connect(machine.emu, 1, 0);
    CHECK_EQ(retro3do_pad_count(machine.emu), 1u);
}

TEST(pulling_from_a_silent_machine_gives_silence_and_admits_it) {
    Machine machine;
    int16_t buffer[64];
    for (int16_t &sample : buffer) {
        sample = 0x7FFF;
    }

    const uint32_t real = retro3do_audio_pull(machine.emu, buffer, 32);
    CHECK_EQ(real, 0u);
    // The shortfall is filled rather than left as whatever the audio thread's
    // buffer happened to contain, which would be a burst of noise on a device.
    for (int16_t sample : buffer) {
        CHECK_EQ(sample, 0);
    }
    CHECK(retro3do_audio_underruns(machine.emu) > 0u);
}

TEST(the_cpu_scale_is_clamped_to_a_machine_that_still_works) {
    Machine machine;
    CHECK_EQ(retro3do_get_cpu_scale_percent(machine.emu), 100u);

    retro3do_set_cpu_scale_percent(machine.emu, 130);
    CHECK_EQ(retro3do_get_cpu_scale_percent(machine.emu), 130u);

    // A damaged settings file must not be able to produce an unusable machine.
    retro3do_set_cpu_scale_percent(machine.emu, 0);
    CHECK_EQ(retro3do_get_cpu_scale_percent(machine.emu), 100u);
    retro3do_set_cpu_scale_percent(machine.emu, 100000);
    CHECK_EQ(retro3do_get_cpu_scale_percent(machine.emu), 150u);
}

TEST(nvram_is_only_handed_out_when_the_machine_has_written_to_it) {
    Machine machine;
    const uint32_t size = retro3do_nvram_size(machine.emu);
    CHECK(size > 0u);

    std::vector<uint8_t> save(size, 0);
    uint32_t written = 0;
    // Nothing has saved a game, so there is nothing to persist. This is the
    // usual answer and is why polling it every frame is cheap.
    CHECK_EQ(retro3do_take_nvram(machine.emu, save.data(), size, &written), 0);

    // A restore of the right size is accepted; the machine formats what it does
    // not recognise rather than refusing to boot.
    CHECK_EQ(retro3do_restore_nvram(machine.emu, save.data(), size), 1);
}

namespace {
void count_frame(void *user) { ++*static_cast<int *>(user); }
}  // namespace

TEST(the_frame_hook_runs_once_per_frame_on_either_driver) {
    Machine machine;
    int frames = 0;
    retro3do_set_frame_hook(machine.emu, &count_frame, &frames);
    retro3do_run_frame(machine.emu);
    retro3do_run_frame(machine.emu);
    CHECK_EQ(frames, 2);

    retro3do_start(machine.emu);
    CHECK(wait_until([&] { return frames >= 5; }));
    retro3do_stop(machine.emu);
    retro3do_set_frame_hook(machine.emu, nullptr, nullptr);
    const int stopped_at = frames;
    retro3do_run_frame(machine.emu);
    CHECK_EQ(frames, stopped_at);
}

TEST(ram_reads_stop_at_the_end_of_dram_and_discs_must_be_present) {
    Machine machine;
    retro3do_run_frame(machine.emu);
    unsigned char bytes[8] = {1, 1, 1, 1, 1, 1, 1, 1};
    CHECK_EQ(retro3do_read_ram(machine.emu, 0x1FFFFC, bytes, 8), 4u);
    CHECK_EQ(int(bytes[4]), 0);
    CHECK_EQ(int(bytes[7]), 0);
    CHECK_EQ(retro3do_read_ram(machine.emu, 0x300000, bytes, 1), 0u);

    unsigned char sector[2048];
    CHECK_EQ(retro3do_read_disc_sectors(machine.emu, 0, 1, sector), 0);
}
