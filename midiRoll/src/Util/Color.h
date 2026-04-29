#pragma once
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace pfd {
namespace util {

struct Color {
    float r{}, g{}, b{}, a{1.0f};

    constexpr Color() = default;
    constexpr Color(float r, float g, float b, float a = 1.0f) : r(r), g(g), b(b), a(a) {}

    static constexpr Color FromRGB(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        return {r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f};
    }

    static Color FromHSV(float h, float s, float v, float alpha = 1.0f) {
        h = std::fmodf(h, 360.0f);
        if (h < 0) h += 360.0f;
        float c = v * s;
        float x = c * (1.0f - std::fabsf(std::fmodf(h / 60.0f, 2.0f) - 1.0f));
        float m = v - c;
        float r{}, g{}, b{};
        if      (h < 60)  { r = c; g = x; b = 0; }
        else if (h < 120) { r = x; g = c; b = 0; }
        else if (h < 180) { r = 0; g = c; b = x; }
        else if (h < 240) { r = 0; g = x; b = c; }
        else if (h < 300) { r = x; g = 0; b = c; }
        else              { r = c; g = 0; b = x; }
        return {r + m, g + m, b + m, alpha};
    }

    Color WithAlpha(float alpha) const { return {r, g, b, alpha}; }

    Color operator*(float s) const { return {r * s, g * s, b * s, a * s}; }
    Color operator+(Color c) const { return {r + c.r, g + c.g, b + c.b, a + c.a}; }

    uint32_t ToRGBA8() const {
        auto u = [](float v) -> uint8_t { return (uint8_t)(std::clamp(v, 0.0f, 1.0f) * 255.0f); };
        return (u(r) << 24) | (u(g) << 16) | (u(b) << 8) | u(a);
    }
};

// 16-channel color palette
inline Color ChannelColor(int ch) {
    static constexpr Color palette[16] = {
        {0.31f, 0.76f, 0.97f},  // 0  light blue
        {1.00f, 0.44f, 0.26f},  // 1  deep orange
        {0.40f, 0.73f, 0.42f},  // 2  green
        {1.00f, 0.79f, 0.16f},  // 3  amber
        {0.67f, 0.28f, 0.74f},  // 4  purple
        {0.94f, 0.33f, 0.31f},  // 5  red
        {0.15f, 0.78f, 0.85f},  // 6  cyan
        {0.55f, 0.43f, 0.39f},  // 7  brown
        {0.47f, 0.56f, 0.61f},  // 8  blue grey
        {0.83f, 0.88f, 0.34f},  // 9  lime
        {1.00f, 0.54f, 0.40f},  // 10 deep orange light
        {0.68f, 0.84f, 0.51f},  // 11 light green
        {0.47f, 0.53f, 0.80f},  // 12 indigo
        {0.94f, 0.38f, 0.57f},  // 13 pink
        {0.30f, 0.71f, 0.67f},  // 14 teal
        {1.00f, 0.84f, 0.31f},  // 15 yellow
    };
    return palette[ch % 16];
}

} // namespace util
} // namespace pfd
