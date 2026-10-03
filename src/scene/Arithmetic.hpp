#pragma once
#include <bit>
#include <cmath>
#include <cstdint>
#include <utility>

namespace scene {

inline uint32_t floatBits(float x) { return std::bit_cast<uint32_t>(x); }
inline float asFloat(uint32_t bits) { return std::bit_cast<float>(bits); }
inline float down(float x) { return asFloat(floatBits(x) - 1u); }
inline int32_t s16(int32_t x) { return static_cast<int16_t>(static_cast<uint16_t>(x)); }
inline int32_t u16(int32_t x) { return x & 0xffff; }

constexpr float kMaxFloat = 3.4028234663852886e38f;
constexpr float kMinNormal = 1.1754943508222875e-38f;

// The EE's single precision (facts/clock-camera.md, References/lib/ee-float.mjs): every result
// cut toward zero, no denormals, saturated at the largest single. Out of line, built strict.
struct EeArithmetic {
    static float cut(double x);
    static float add(float a, float b);
    static float sub(float a, float b);
    static float mul(float a, float b);
    static float div(float a, float b);
    static float sqrt(float a);
    static float divExact(float a, float b);
    static float sqrtExact(float a);
    static int32_t toInt(float x);
    static uint32_t toUnsigned(float x);
    static float sin16(int32_t angle);
    static float cos16(int32_t angle);
    static float sinf(float x);
    static float cosf(float x);
    static std::pair<float, float> sineCosine(float radians);
};

struct NativeArithmetic {
    static float cut(double x) { return static_cast<float>(x); }
    static float add(float a, float b) { return a + b; }
    static float sub(float a, float b) { return a - b; }
    static float mul(float a, float b) { return a * b; }
    static float div(float a, float b) { return a / b; }
    static float sqrt(float a) { return std::sqrt(a); }
    static float divExact(float a, float b) { return a / b; }
    static float sqrtExact(float a) { return std::sqrt(std::fabs(a)); }
    static int32_t toInt(float x) {
        if (std::isnan(x)) return 0;
        if (x >= 2147483647.0f) return INT32_MAX;
        if (x <= -2147483648.0f) return INT32_MIN;
        return static_cast<int32_t>(x);
    }
    static uint32_t toUnsigned(float x) { return x > 0.0f && x < 4294967296.0f ? static_cast<uint32_t>(x) : 0u; }
    static float sin16(int32_t angle) {
        const int32_t a = s16(angle);
        int32_t v = a < 0 ? -a : a;
        if (v >= 0x4000) v = 0x8000 - v;
        const float s = std::sin(static_cast<float>(v) * (1.5707963267948966f / 16385.0f));
        return a < 0 ? -s : s;
    }
    static float cos16(int32_t angle) { return sin16(s16(angle) + 0x4000); }
    static float sinf(float x) { return std::sin(x); }
    static float cosf(float x) { return std::cos(x); }
    static std::pair<float, float> sineCosine(float radians) { return {std::sin(radians), std::cos(radians)}; }
};

}
