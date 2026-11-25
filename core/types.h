// Shared fixed-width types for the 3DO core.
#pragma once

#include <cstddef>
#include <cstdint>

// For the few small functions on the hottest paths that the compiler will not
// always inline by itself, where a call per emulated instruction shows up.
#if defined(_MSC_VER)
#define RETRO3DO_ALWAYS_INLINE __forceinline
#elif defined(__GNUC__)
#define RETRO3DO_ALWAYS_INLINE inline __attribute__((always_inline))
#else
#define RETRO3DO_ALWAYS_INLINE inline
#endif

namespace retro3do {

using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using s8  = std::int8_t;
using s16 = std::int16_t;
using s32 = std::int32_t;
using s64 = std::int64_t;

// Rotate right, the ARM barrel shifter's basic operation. Written so the
// compiler recognises it and emits a single ROR; the naive `(x >> n) | (x << (32
// - n))` is undefined when n == 0, which the ARM immediate decoder does produce.
constexpr u32 ror32(u32 value, unsigned amount) noexcept {
    amount &= 31u;
    return amount == 0 ? value : ((value >> amount) | (value << (32u - amount)));
}

}  // namespace retro3do
