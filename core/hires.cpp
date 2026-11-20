#include "hires.h"

#include <algorithm>

namespace retro3do {

void HiresStore::configure(u32 scale) {
    if (scale < 1u) scale = 1u;
    if (scale > kMaxScale) scale = kMaxScale;
    if (scale == scale_ && (scale == 1u || !subs_.empty())) {
        clear();
        return;
    }
    scale_ = scale;
    area_ = scale * scale;
    if (scale == 1u) {
        std::vector<u16>().swap(subs_);
        std::vector<u16>().swap(echo_);
        std::vector<u8>().swap(valid_);
        std::vector<u32>().swap(stamp_);
        return;
    }
    subs_.assign(static_cast<size_t>(kSlots) * area_, 0);
    echo_.assign(kSlots, 0);
    valid_.assign(kSlots, 0);
    stamp_.assign(kSlots, 0);
}

void HiresStore::clear() {
    std::fill(valid_.begin(), valid_.end(), u8{0});
    clear_stamps();
}

void HiresStore::clear_stamps() {
    std::fill(stamp_.begin(), stamp_.end(), 0u);
}

}  // namespace retro3do
