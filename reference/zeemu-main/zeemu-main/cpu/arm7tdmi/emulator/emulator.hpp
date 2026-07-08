// Minimal nall/higan compatibility shim for the ARM7TDMI core.
// Provides only what the arm7tdmi source files actually use.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <optional>
#include <type_traits>
#include <cassert>
#include <initializer_list>
#include <cmath>

namespace nall {

// ---------------------------------------------------------------------------
// uint — plain unsigned int (matches nall's typedef)
// ---------------------------------------------------------------------------
using uint = unsigned int;

// ---------------------------------------------------------------------------
// Natural<N> — N-bit unsigned integer with .bit() accessor and masking
// ---------------------------------------------------------------------------
template<unsigned N>
struct Natural {
    static_assert(N > 0 && N <= 64, "Natural bit width out of range");
    using T = std::conditional_t<(N<=8),  uint8_t,
              std::conditional_t<(N<=16), uint16_t,
              std::conditional_t<(N<=32), uint32_t, uint64_t>>>;
    static constexpr T mask = (N == 64) ? T(~uint64_t(0)) : T((uint64_t(1) << N) - 1);

    T data = 0;

    Natural() = default;
    template<typename V> Natural(V v) : data(T(v) & mask) {}
    template<unsigned M> Natural(Natural<M> v) : data(T(v.data) & mask) {}

    operator T() const { return data; }

    template<typename V>   auto& operator=(V v)           { data = T(v)      & mask; return *this; }
    template<unsigned M>   auto& operator=(Natural<M> v)  { data = T(v.data) & mask; return *this; }

    // bit extraction
    bool bit(unsigned n) const { return bool((data >> n) & T(1)); }
    T    bit(unsigned lo, unsigned hi) const {
        unsigned w = hi - lo + 1;
        T m = (w >= sizeof(T)*8) ? T(~uint64_t(0)) : T((uint64_t(1) << w) - 1);
        return (data >> lo) & m;
    }

    // compound assignment — keep masking
    template<typename V> auto& operator+=(V v) { data = T(data + T(v)) & mask; return *this; }
    template<typename V> auto& operator-=(V v) { data = T(data - T(v)) & mask; return *this; }
    template<typename V> auto& operator*=(V v) { data = T(data * T(v)) & mask; return *this; }
    template<typename V> auto& operator|=(V v) { data = (data | T(v))  & mask; return *this; }
    template<typename V> auto& operator&=(V v) { data = (data & T(v));         return *this; }
    template<typename V> auto& operator^=(V v) { data = (data ^ T(v))  & mask; return *this; }
};

// ---------------------------------------------------------------------------
// Integer<N> — N-bit signed integer (sign-extends on construction)
// ---------------------------------------------------------------------------
template<unsigned N>
struct Integer {
    static_assert(N > 0 && N <= 64, "Integer bit width out of range");
    using T = std::conditional_t<(N<=8),  int8_t,
              std::conditional_t<(N<=16), int16_t,
              std::conditional_t<(N<=32), int32_t, int64_t>>>;
    using U = std::make_unsigned_t<T>;
    static constexpr U umask = (N == 64) ? U(~uint64_t(0)) : U((uint64_t(1) << N) - 1);
    static constexpr U usign  = U(uint64_t(1) << (N - 1));

    T data = 0;

    Integer() = default;
    template<typename V> Integer(V v) {
        U u = U(v) & umask;
        data = (u & usign) ? T(u | ~umask) : T(u);
    }

    operator T() const { return data; }
    template<typename V> auto& operator=(V v) { *this = Integer(v); return *this; }
};

// ---------------------------------------------------------------------------
// boolean — 1-bit bool
// ---------------------------------------------------------------------------
struct boolean {
    bool data = false;
    boolean() = default;
    boolean(bool v) : data(v) {}
    template<typename V> boolean(V v) : data(bool(v)) {}
    operator bool() const { return data; }
    auto& operator=(bool v)    { data = v; return *this; }
    template<typename V>
    auto& operator=(V v) { data = bool(v); return *this; }
    bool operator==(bool     v) const { return data == v; }
    bool operator!=(bool     v) const { return data != v; }
    bool operator==(boolean  v) const { return data == v.data; }
    bool operator!=(boolean  v) const { return data != v.data; }
};

// ---------------------------------------------------------------------------
// function<Sig> — thin wrapper over std::function
// Handles both void() and auto()->void syntaxes (same C++ type)
// ---------------------------------------------------------------------------
template<typename Sig>
struct function : std::function<Sig> {
    using std::function<Sig>::function;
    using std::function<Sig>::operator=;
    function() = default;
};

// ---------------------------------------------------------------------------
// string — std::string with nall-compatible extras
// ---------------------------------------------------------------------------
struct string : std::string {
    using std::string::string;
    string() = default;
    string(const std::string& s) : std::string(s) {}
    string(std::string&& s)      : std::string(std::move(s)) {}

    // construct from integral types (used in disassembler: " lsr #", shift)
    string(unsigned      v) : std::string(std::to_string(v)) {}
    string(int           v) : std::string(std::to_string(v)) {}
    string(long          v) : std::string(std::to_string(v)) {}
    string(unsigned long v) : std::string(std::to_string(v)) {}

    // construct from Natural<N> (avoids two-step UDC through operator T())
    template<unsigned N>
    string(Natural<N> v) : std::string(std::to_string((unsigned long long)v.data)) {}
    template<unsigned N>
    string(Integer<N> v) : std::string(std::to_string((long long)v.data)) {}

    // initializer-list constructor: string{"a", "b", ...} concatenates
    string(std::initializer_list<string> list) {
        for (const auto& s : list) std::string::append(s);
    }

    // variadic append (used in disassembleContext)
    template<typename... Args>
    string& append(Args&&... args) {
        (_append1(args), ...);
        return *this;
    }

    // trimRight: remove up to `count` trailing occurrences of `suffix`
    string& trimRight(const char* suffix, long count = 1L) {
        if (!suffix) return *this;
        size_t slen = strlen(suffix);
        if (!slen) return *this;
        for (long i = 0; i < count; ++i) {
            if (size() >= slen && compare(size() - slen, slen, suffix) == 0)
                resize(size() - slen);
            else break;
        }
        return *this;
    }

private:
    void _append1(const string&      s) { std::string::append(s); }
    void _append1(const std::string& s) { std::string::append(s); }
    void _append1(const char*        s) { if (s) std::string::append(s); }
    template<typename T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
    void _append1(T v) { std::string::append(std::to_string((long long)v)); }
};

// ---------------------------------------------------------------------------
// serializer — stub (serialize/deserialize not needed for emulation)
// ---------------------------------------------------------------------------
struct serializer {
    template<typename T> void integer(T&) {}
    template<typename T> void boolean(T&) {}
};

// ---------------------------------------------------------------------------
// maybe<T> — std::optional with nall's operator() accessor
// ---------------------------------------------------------------------------
template<typename T>
struct maybe : std::optional<T> {
    using std::optional<T>::optional;
    maybe() : std::optional<T>() {}
    T operator()() const { return this->value(); }
};

// ---------------------------------------------------------------------------
// range — iterable [0, n)
// ---------------------------------------------------------------------------
struct range_t {
    unsigned n;
    struct iterator {
        unsigned i;
        unsigned    operator*()  const { return i; }
        iterator&   operator++()       { ++i; return *this; }
        bool        operator!=(const iterator& o) const { return i != o.i; }
    };
    [[nodiscard]] iterator begin() const { return {0}; }
    [[nodiscard]] iterator end()   const { return {n}; }
};
inline range_t range(unsigned n) { return {n}; }

// ---------------------------------------------------------------------------
// bit utilities
// ---------------------------------------------------------------------------
namespace bit {
    // test(pattern): compile-time parse of bit pattern string → uint32
    // '1' sets bit, everything else (. ? 0 - space) leaves bit clear.
    // MSB-first: left char = highest bit. Width determined by non-space char count.
    constexpr uint32_t test(const char* p) {
        // Count non-space chars to find pattern width
        int len = 0;
        for (const char* q = p; *q; ++q)
            if (*q != ' ') ++len;
        uint32_t result = 0;
        int pos = len - 1;
        for (; *p; ++p) {
            if (*p == ' ') continue;
            if (*p == '1') result |= (uint32_t(1) << pos);
            --pos;
        }
        return result;
    }

    // count set bits
    constexpr uint32_t count(uint64_t v) {
        uint32_t c = 0;
        while (v) { c += uint32_t(v & 1); v >>= 1; }
        return c;
    }
}

// ---------------------------------------------------------------------------
// hex / pad — disassembler helpers
// ---------------------------------------------------------------------------
inline string hex(uint64_t v, long digits = 0) {
    char buf[32];
    if (digits > 0)
        snprintf(buf, sizeof(buf), "%0*llx", (int)digits, (unsigned long long)v);
    else
        snprintf(buf, sizeof(buf), "%llx", (unsigned long long)v);
    return string(buf);
}

inline string pad(const string& s, long width) {
    if (width < 0) {
        string r = s;
        while ((long)r.size() < -width) r += ' ';
        return r;
    }
    string r = s;
    while ((long)r.size() < width) r = ' ' + r;
    return r;
}

// ---------------------------------------------------------------------------
// Type aliases matching nall naming
// ---------------------------------------------------------------------------
using uint1  = Natural<1>;
using uint2  = Natural<2>;
using uint3  = Natural<3>;
using uint4  = Natural<4>;
using uint5  = Natural<5>;
using uint6  = Natural<6>;
using uint7  = Natural<7>;
using uint8  = Natural<8>;
using uint10 = Natural<10>;
using uint11 = Natural<11>;
using uint12 = Natural<12>;
using uint15 = Natural<15>;
using uint16 = Natural<16>;
using uint24 = Natural<24>;
using uint32 = Natural<32>;
using uint64 = Natural<64>;

using int8   = Integer<8>;
using int11  = Integer<11>;
using int16  = Integer<16>;
using int22  = Integer<22>;
using int24  = Integer<24>;
using int32  = Integer<32>;
using int64  = Integer<64>;

} // namespace nall

// ---------------------------------------------------------------------------
// unreachable hint
// ---------------------------------------------------------------------------
#ifndef unreachable
#  if defined(__GNUC__) || defined(__clang__)
#    define unreachable __builtin_unreachable()
#  elif defined(_MSC_VER)
#    define unreachable __assume(false)
#  else
#    define unreachable ((void)0)
#  endif
#endif

// Bring nall types into higan namespace
namespace higan { using namespace nall; }
