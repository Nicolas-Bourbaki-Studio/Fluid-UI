#pragma once
#include <algorithm>
#include <cstdint>
#include <string_view>
#include <vector>

namespace fluid {

using WidgetId = uint32_t;
using GPUImageId = uint64_t;

constexpr uint32_t kHashSeed = 2166136261u;

constexpr uint32_t HashStr(std::string_view s, uint32_t seed = kHashSeed) {
    uint32_t h = seed;
    for (char c : s) { h ^= static_cast<uint8_t>(c); h *= 16777619u; }
    return h;
}

struct Vec2 {
    float x = 0.f, y = 0.f;
    constexpr Vec2() = default;
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}
    constexpr Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(float s) const { return {x * s, y * s}; }
};

struct Rect {
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
    constexpr bool Contains(Vec2 p) const {
        return p.x >= x && p.y >= y && p.x < x + w && p.y < y + h;
    }
    constexpr Rect Expanded(float a) const { return {x - a, y - a, w + 2 * a, h + 2 * a}; }
    Rect Intersect(const Rect& o) const {
        float x0 = std::max(x, o.x), y0 = std::max(y, o.y);
        float x1 = std::min(x + w, o.x + o.w), y1 = std::min(y + h, o.y + o.h);
        return {x0, y0, std::max(0.f, x1 - x0), std::max(0.f, y1 - y0)};
    }
    constexpr bool operator==(const Rect& o) const {
        return x == o.x && y == o.y && w == o.w && h == o.h;
    }
};

struct Color {
    float r = 1.f, g = 1.f, b = 1.f, a = 1.f;

    // 0xRRGGBBAA
    static constexpr Color FromHex(uint32_t rgba) {
        return Color{((rgba >> 24) & 255) / 255.f, ((rgba >> 16) & 255) / 255.f,
                     ((rgba >> 8) & 255) / 255.f, (rgba & 255) / 255.f};
    }
    // Packed as R | G<<8 | B<<16 | A<<24 (matches RGBA8 unsigned-byte vertex colors)
    uint32_t ToU32() const {
        auto c = [](float v) { return static_cast<uint32_t>(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f); };
        return c(r) | (c(g) << 8) | (c(b) << 16) | (c(a) << 24);
    }
};

struct GradientStop {
    Color color;
    float position = 0.f;
};

struct LinearGradient {
    float angleDegrees = 180.f;
    std::vector<GradientStop> stops;

    bool Empty() const { return stops.size() < 2; }
};

struct Edges { // top, right, bottom, left
    float top = 0, right = 0, bottom = 0, left = 0;
    constexpr Edges() = default;
    constexpr explicit Edges(float all) : top(all), right(all), bottom(all), left(all) {}
    constexpr Edges(float v, float h) : top(v), right(h), bottom(v), left(h) {}
    constexpr Edges(float t, float r, float b, float l) : top(t), right(r), bottom(b), left(l) {}
    constexpr float Horizontal() const { return left + right; }
    constexpr float Vertical() const { return top + bottom; }
};

inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
inline Color Lerp(Color a, Color b, float t) {
    return {Lerp(a.r, b.r, t), Lerp(a.g, b.g, t), Lerp(a.b, b.b, t), Lerp(a.a, b.a, t)};
}
inline Edges Lerp(Edges a, Edges b, float t) {
    return {Lerp(a.top, b.top, t), Lerp(a.right, b.right, t),
            Lerp(a.bottom, b.bottom, t), Lerp(a.left, b.left, t)};
}

}
