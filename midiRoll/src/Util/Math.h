#pragma once
#include <cmath>
#include <algorithm>

namespace pfd {
namespace util {

constexpr float PI  = 3.14159265358979323846f;
constexpr float TAU = 6.28318530717958647692f;

struct Vec2 {
    float x{}, y{};

    Vec2() = default;
    constexpr Vec2(float x, float y) : x(x), y(y) {}

    Vec2 operator+(Vec2 v) const { return {x + v.x, y + v.y}; }
    Vec2 operator-(Vec2 v) const { return {x - v.x, y - v.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    Vec2& operator+=(Vec2 v) { x += v.x; y += v.y; return *this; }

    float Length() const { return std::sqrtf(x * x + y * y); }
    Vec2  Normalized() const { float l = Length(); return l > 0 ? Vec2{x/l, y/l} : Vec2{0,0}; }
};

struct Vec4 {
    float x{}, y{}, z{}, w{};
    constexpr Vec4() = default;
    constexpr Vec4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
};

inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
inline Vec2 Lerp(Vec2 a, Vec2 b, float t) { return {Lerp(a.x, b.x, t), Lerp(a.y, b.y, t)}; }

inline float Clamp(float v, float lo, float hi) { return std::clamp(v, lo, hi); }

// Easing functions
inline float EaseOutCubic(float t) { return 1.0f - std::powf(1.0f - t, 3.0f); }
inline float EaseOutQuad(float t)  { return 1.0f - (1.0f - t) * (1.0f - t); }
inline float EaseInQuad(float t)   { return t * t; }
inline float EaseInOutSine(float t){ return -(std::cosf(PI * t) - 1.0f) / 2.0f; }

// Smoothstep (HLSL-compatible)
inline float Smoothstep(float edge0, float edge1, float x) {
    float t = Clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace util
} // namespace pfd
