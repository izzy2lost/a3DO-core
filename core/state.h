// Save states: one pass over the machine that either writes it out or reads it
// back.
//
// Every component has a single serialize(StateIO&) that names its fields once.
// The same function saves and loads, so the two can never disagree about order
// or size - the usual way a save-state format rots is a field added to the
// writer and forgotten in the reader.
//
// What is saved is the machine, not the host: pointers between components,
// handler callbacks, decode caches and diagnostics counters are left alone, and
// anything derived (the ARM decode cache, the high-resolution sub-pixels) is
// rebuilt after a load rather than stored.
#pragma once

#include <array>
#include <cstring>
#include <deque>
#include <string>
#include <type_traits>
#include <vector>

#include "types.h"

namespace retro3do {

class StateIO {
public:
    // Writing: everything appended to `out`.
    explicit StateIO(std::vector<u8>& out) : out_(&out) {}
    // Reading: from `data`, `size` bytes.
    StateIO(const u8* data, size_t size) : in_(data), size_(size) {}

    bool loading() const { return in_ != nullptr; }
    bool ok() const { return ok_; }
    const std::string& error() const { return error_; }
    void fail(const std::string& why) {
        if (ok_) error_ = why;
        ok_ = false;
    }

    void raw(void* p, size_t n) {
        if (!ok_) return;
        if (out_) {
            const u8* b = static_cast<const u8*>(p);
            out_->insert(out_->end(), b, b + n);
            return;
        }
        if (pos_ + n > size_) {
            fail("the state ends early");
            std::memset(p, 0, n);
            return;
        }
        std::memcpy(p, in_ + pos_, n);
        pos_ += n;
    }

    template <typename T>
    void operator()(T& v) {
        static_assert(std::is_trivially_copyable_v<T>, "save field must be plain data");
        raw(&v, sizeof v);
    }

    template <typename T>
    void vec(std::vector<T>& v) {
        static_assert(std::is_trivially_copyable_v<T>, "save field must be plain data");
        u32 n = static_cast<u32>(v.size());
        (*this)(n);
        if (!ok_) return;
        if (loading()) {
            if (static_cast<u64>(n) * sizeof(T) > size_ - pos_) {
                fail("the state ends early");
                return;
            }
            v.resize(n);
        }
        if (n) raw(v.data(), n * sizeof(T));
    }

    // A buffer whose size the machine fixes (RAM, VRAM). A state for a
    // differently-sized machine is refused rather than resized into it.
    template <typename T>
    void fixed(std::vector<T>& v, const char* what) {
        u32 n = static_cast<u32>(v.size());
        (*this)(n);
        if (!ok_) return;
        if (loading() && n != v.size()) {
            fail(std::string("the state's ") + what + " is a different size");
            return;
        }
        if (n) raw(v.data(), n * sizeof(T));
    }

    void bytes(std::deque<u8>& d) {
        std::vector<u8> v(d.begin(), d.end());
        vec(v);
        if (loading() && ok_) d.assign(v.begin(), v.end());
    }

    // A four-letter tag between components. Costs nothing and turns a
    // misaligned read into "section MADM missing" instead of a machine that
    // boots into garbage.
    void section(const char tag[5]) {
        u32 want = 0;
        std::memcpy(&want, tag, 4);
        u32 got = want;
        (*this)(got);
        if (ok_ && got != want) fail(std::string("section ") + tag + " missing");
    }

private:
    std::vector<u8>* out_ = nullptr;
    const u8* in_ = nullptr;
    size_t size_ = 0;
    size_t pos_ = 0;
    bool ok_ = true;
    std::string error_;
};

}  // namespace retro3do
