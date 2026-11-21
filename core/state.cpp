// Save states. See state.h for the shape of it; this file is the field lists.
//
// Each serialize() names the fields that ARE the machine. Left out on purpose,
// everywhere: references and pointers to other parts, handler callbacks,
// decode caches (rebuilt), diagnostics counters, and per-frame scratch that is
// empty at a frame boundary, which is the only place a state is taken.
#include "state.h"

#include <cstdio>

#include "arm60.h"
#include "bus.h"
#include "clio.h"
#include "console.h"
#include "dsp.h"
#include "madam.h"
#include "sport.h"
#include "vdlp.h"
#include "xbus.h"

namespace retro3do {

namespace {

// "A3DOSAVE", then the format version. Bump it whenever any field list below
// changes; an older state is then refused cleanly instead of misread.
constexpr char kMagic[8] = {'A', '3', 'D', 'O', 'S', 'A', 'V', 'E'};
constexpr u32 kVersion = 1;

u64 fnv1a(const u8* data, size_t size, u64 h = 0xcbf29ce484222325ull) {
    for (size_t i = 0; i < size; ++i) {
        h ^= data[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

}  // namespace

void Arm60::serialize(StateIO& s) {
    s.section("ARM6");
    s(regs_);
    s(cpsr_);
    s(banked_r13_);
    s(banked_r14_);
    s(banked_spsr_);
    s(banked_fiq_r8_r12_);
    s(banked_user_r8_r12_);
    s(pc_written_);
    s(irq_line_);
    s(irq_latched_);
    s(fiq_line_);
    s(total_cycles_);
    s(total_instructions_);
    // CLIO compares its own copy against this to tell which interrupts the
    // CPU has accepted, so the two travel together.
    s(irqs_taken_);
    s(fiqs_taken_);
    if (s.loading()) invalidate_decode_cache();
}

void Bus::serialize(StateIO& s) {
    s.section("BUS ");
    // The ROM is not stored - it is the user's BIOS, and the state is only
    // meaningful on top of the one it was made with.
    u64 rom_hash = fnv1a(rom_.data(), rom_.size());
    const u64 have = rom_hash;
    s(rom_hash);
    if (s.loading() && s.ok() && rom_hash != have) {
        s.fail("it was made with a different BIOS");
        return;
    }
    s(rom_overlay_);
    s.fixed(dram_, "main memory");
    s.fixed(vram_, "video memory");
    s.fixed(nvram_, "NVRAM");
    s(cel_pending_);
    // What the game had saved to NVRAM at that moment is what it now
    // remembers, so the file on disk follows.
    if (s.loading()) nvram_dirty_ = true;
}

void CdRomDevice::serialize(StateIO& s) {
    s.section("CDRM");
    s.bytes(status_);
    s.bytes(pending_);
    s.vec(pending_reply_);
    s(interrupt_request_);
    s(disc_present_);
    s(media_changed_);
    s(motor_on_);
    s(tray_open_);
    s(ready_);
    s(last_error_);
    s(sector_delay_);
    s(transfer_lba_);
    s(transfer_sectors_);
    s(sector_size_);
    s.vec(data_);
    u64 pos = data_pos_;
    s(pos);
    data_pos_ = static_cast<size_t>(pos);
    if (s.loading() && data_pos_ > data_.size()) s.fail("the drive buffer is damaged");
    s(commands_);
    s(last_command_);
    s.vec(last_command_bytes_);
}

void Clio::serialize(StateIO& s) {
    s.section("CLIO");
    s(signalled_);
    s(last_irqs_taken_);
    s(irq0_pending_);
    s(irq0_enabled_);
    s(irq1_pending_);
    s(irq1_enabled_);
    s(timer_delay_);
    s(timer_accumulator_);
    s(timer_counter_);
    s(timer_reload_);
    s(timer_config_);
    s(control_);
    s(xbus_sel_);
    s(xbus_dma_enable_);
    s(xbus_poll_);
    s(xbus_device_poll_);
    s(xbus_sel_modifier_);
    s(media_changed_);
    s(xbus_dma_requested_);
    s(xbus_control_);
    s(xbus_direction_);
    s(xbus_type0_);
    s(xbus_xfer_count_);
    s(dipir1_);
    s(dipir2_);
    s(vint0_line_);
    s(vint1_line_);
    s(scanline_);
    s(pixel_in_line_);
    s(scanlines_per_field_);
    s(field_complete_);
    s(field_odd_);
    s(irq_asserted_);
    s(revision_);
    s(random_state_);
    s(mode_);
    s(csys_bits_);
    s(cstat_bits_);
    s(audio_in_);
    s(audio_out_);
    s(spare_);
    s(hdelay_);
    s(adbctl_);
    s(watchdog_);
    s(seed_);
    s(timer_slack_);
    s(written_value_);
    s(written_flag_);
    s.vec(dsp_window_);
    s(xbus_result_);
    cdrom_.serialize(s);
}

void Madam::serialize(StateIO& s) {
    s.section("MADM");
    s(revision_);
    s(mem_config_);
    s(dma_enable_);
    s(xbus_dma_address_);
    s(xbus_dma_length_);
    s(xbus_dma_pending_);
    s(vdl_address_);
    s(input_fifo_);
    s(output_fifo_);
    s(dma_channel_enable_);
    s(pbus_address_);
    s(pbus_length_);
    s(pbus_pointer_);
    s(current_ccb_);
    s(next_ccb_);
    s(cel_engine_state_);
    s(cel_walk_started_);
    s(dma_address_);
    s(dma_length_);
    s(clip_width_);
    s(clip_height_);
    s(reg_ctl0_);
    s(reg_ctl1_);
    s(read_base_);
    s(write_base_);
    s(read_stride_);
    s(write_stride_);
    s(framebuffer_configured_);
    s(ccb_ctl0_);
    s(cel_flags_);
    s(cel_pixc_);
    s(cel_pre1_);
    s(cel_pxor1_);
    s(cel_pxor2_);
    s(cel_pmode_or_);
    s(cel_pmode_and_);
    s(cel_origin_vh_);
    s(cel_bpp_);
    s(cel_linear_);
    s(cel_pluta_);
    s(cel_pixel_mask_);
    s(cel_transparent_mask_);
    s(ccb_x_register_);
    s(ccb_y_register_);
    s(ccb_hdx_register_);
    s(ccb_hdy_register_);
    s(ccb_vdx_register_);
    s(ccb_vdy_register_);
    s(ccb_hddx_register_);
    s(ccb_hddy_register_);
    s(ccb_pixc_register_);
    s(pre0_register_);
    s(pre1_register_);
    s(matrix_in_);
    s(matrix_vec_);
    s(matrix_out_);
    s(matrix_pending_);
    s(matrix_num_hi_);
    s(matrix_num_lo_);
    s(cel_one_to_one_);
    s(cel_mapper_);
    s(plut_);
    s(target_address_);
    s(target_stride_bytes_);
    s(written_value_);
    s(written_flag_);
}

void Dsp::serialize(StateIO& s) {
    s.section("DSPP");
    // decoded_, branch_taken_ and register_map_ are tables built once from
    // nothing but the instruction set, so they are the same in every machine.
    s(program_);
    s(data_);
    s(cpu_supplied_);
    s(pc_);
    s(register_base_x4_);
    s(register_map_index_);
    s(audio_out_status_);
    s(semaphore_status_);
    s(semaphore_data_);
    s(interrupt_word_);
    s(counter_);
    s(reload_);
    s(mult1_);
    s(mult2_);
    s(alu1_);
    s(alu2_);
    s(barrel_shift_);
    s(operand_mask_);
    s(writeback_);
    s(requests_);
    s(running_);
    s(generate_interrupt_);
    s(noise_state_);
    if (s.loading()) {
        if (register_map_index_ >= register_map_.size()) s.fail("the DSP state is damaged");
        pc_ &= static_cast<u32>(program_.size() - 1);
    }
}

void Sport::serialize(StateIO& s) {
    s.section("SPRT");
    s(source_address_);
    s(fill_value_);
}

void Vdlp::serialize(StateIO& s) {
    s.section("VDLP");
    s(list_address_);
    s(modulo_index_);
    s(clut_red_);
    s(clut_green_);
    s(clut_blue_);
    s(background_);
    s(clut_bypass_);
    s(horizontal_interp_);
    s(vertical_interp_);
    s(disable_vertical_once_);
    s(field_entry_);
    s(field_buffer_);
    s(field_persist_);
    s(field_width_);
    s(field_height_);
    s(field_have_buffer_);
    s(field_dma_enabled_);
    s(field_list_ended_);
    s(field_active_);
    s(first_visible_line_);
    s(total_lines_);
    s(entries_walked_);
}

std::vector<u8> Console::save_state() {
    std::vector<u8> out;
    out.reserve(4u << 20);
    StateIO s(out);
    char magic[8];
    std::memcpy(magic, kMagic, sizeof magic);
    s(magic);
    u32 version = kVersion;
    s(version);

    // Which disc: its shape and a hash of its first sector. Enough to tell
    // one game from another without reading the whole image.
    u32 sectors = disc_.is_open() ? disc_.sector_count() : 0;
    u32 tracks = disc_.is_open() ? static_cast<u32>(disc_.tracks().size()) : 0;
    u64 first = 0;
    if (disc_.is_open()) {
        std::vector<u8> sector(2048);
        if (disc_.read_sector(0, sector.data())) first = fnv1a(sector.data(), sector.size());
    }
    s(sectors);
    s(tracks);
    s(first);

    s.section("CNSL");
    s(region_);
    s(sample_accumulator_);
    s(last_sample_);
    s(expansion_dma_count_);
    s(frame_count_);

    bus_.serialize(s);
    cpu_.serialize(s);
    clio_.serialize(s);
    madam_.serialize(s);
    dsp_.serialize(s);
    sport_.serialize(s);
    vdlp_.serialize(s);
    s.section("END ");
    return out;
}

bool Console::load_state(const u8* data, size_t size) {
    // Read into a scratch copy of nothing: the header first, so a state for
    // another game or another build is refused before anything is touched.
    StateIO s(data, size);
    char magic[8] = {};
    s(magic);
    if (!s.ok() || std::memcmp(magic, kMagic, sizeof magic) != 0) {
        last_error_ = "That is not an a3DO save state.";
        return false;
    }
    u32 version = 0;
    s(version);
    if (version != kVersion) {
        last_error_ = "That save state is from a different version of a3DO.";
        return false;
    }
    u32 sectors = 0, tracks = 0;
    u64 first = 0;
    s(sectors);
    s(tracks);
    s(first);
    const u32 have_sectors = disc_.is_open() ? disc_.sector_count() : 0;
    const u32 have_tracks = disc_.is_open() ? static_cast<u32>(disc_.tracks().size()) : 0;
    u64 have_first = 0;
    if (disc_.is_open()) {
        std::vector<u8> sector(2048);
        if (disc_.read_sector(0, sector.data())) have_first = fnv1a(sector.data(), sector.size());
    }
    if (!s.ok() || sectors != have_sectors || tracks != have_tracks || first != have_first) {
        last_error_ = "That save state is for a different disc.";
        return false;
    }

    // The machine as it is now, so a state that turns out to be damaged
    // halfway through leaves the game running instead of half-loaded.
    std::vector<u8> undo = save_state();

    s.section("CNSL");
    Region region = region_;
    s(region);
    // Before the parts are read: changing region reprograms some of them,
    // and the saved values must be what is left afterwards.
    if (s.ok() && region != region_ &&
        (region == Region::Ntsc || region == Region::Pal))
        set_region(region);
    s(sample_accumulator_);
    s(last_sample_);
    s(expansion_dma_count_);
    s(frame_count_);
    bus_.serialize(s);
    cpu_.serialize(s);
    clio_.serialize(s);
    madam_.serialize(s);
    dsp_.serialize(s);
    sport_.serialize(s);
    vdlp_.serialize(s);
    s.section("END ");

    if (!s.ok()) {
        const std::string why = s.error();
        load_state(undo.data(), undo.size());
        last_error_ = "That save state could not be loaded: " + why + ".";
        return false;
    }
    // Derived from VRAM, so start it over; the next cels rebuild it.
    hires_.clear();
    audio_.reset();
    samples_.clear();
    return true;
}

}  // namespace retro3do
