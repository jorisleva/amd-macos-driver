// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace offscreen {
constexpr uint32_t kGuardBytes = 64;
constexpr uint8_t kGuardValue = 0xa7;
using Pixel = std::array<uint8_t, 4>;
using Color = std::array<float, 4>;
struct Size { uint32_t width, height; };
struct Reference { std::vector<uint8_t> pixels; uint32_t covered = 0, overlap = 0; };
struct Difference { uint32_t pixels = 0, channels = 0, maxError = 0, guards = 0, upload = 0; };
struct Case {
    std::string scene, name, outputChecksum, referenceChecksum;
    uint32_t width, height, iteration, tolerance, covered, overlap;
    Difference differences;
};
inline Pixel background(uint32_t iteration) {
    return {uint8_t(17 + 3 * iteration), uint8_t(29 + 5 * iteration), uint8_t(43 + 7 * iteration), uint8_t(79 + 72 * iteration)};
}
inline Color color(uint32_t iteration, uint32_t draw, bool blend) {
    const std::array<Color, 3> colors{{{0.75f, 0.25f, 0.5f, 1}, {0.25f, 0.5f, 0.75f, 1}, {0.5f, 0.75f, 0.25f, 1}}};
    auto c = colors[(iteration + draw) % colors.size()];
    if (blend) c[3] = draw ? std::array<float, 3>{0.75f, 0.25f, 0.5f}[iteration] : std::array<float, 3>{0.25f, 0.5f, 0.75f}[iteration];
    return c;
}
inline uint8_t unorm(float value) {
    return uint8_t(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}
inline Pixel shade(const Pixel& dst, const Color& src, bool blend) {
    Pixel out{};
    for (size_t c = 0; c < 4; ++c) {
        const float value = blend ? (c == 3 ? src[c] : src[c] * src[3]) + (float(dst[c]) / 255.0f) * (1 - src[3]) : src[c];
        out[c] = unorm(value);
    }
    return out;
}
inline std::vector<uint8_t> texture(Size size, uint32_t iteration) {
    std::vector<uint8_t> bytes(size_t(size.width) * size.height * 4);
    for (uint32_t y = 0; y < size.height; ++y) for (uint32_t x = 0; x < size.width; ++x) {
        const size_t p = (size_t(y) * size.width + x) * 4;
        bytes[p] = uint8_t(x * 17 + y * 3 + iteration * 11);
        bytes[p + 1] = uint8_t(x * 5 + y * 29 + iteration * 7);
        bytes[p + 2] = uint8_t((x ^ y) * 13 + iteration * 31);
        bytes[p + 3] = uint8_t(255 - ((x * 7 + y * 11 + iteration * 5) & 63));
    }
    return bytes;
}
inline bool covered(Size size, uint32_t x, uint32_t y, uint32_t shape) {
    // Vertices match exact binary NDC constants in triangle.vert. The y offset
    // of 1/64 NDC avoids samples exactly on an edge at all committed test sizes.
    // 8 fractional bits represent all viewport-transformed vertices exactly.
    using Point = std::array<int64_t, 2>;
    const int64_t w = size.width, h = size.height;
    const std::array<Point, 3> tri = shape ? std::array<Point, 3>{{{w * 192, h * 66}, {w * 192, h * 194}, {w * 64, h * 66}}}
        : std::array<Point, 3>{{{w * 64, h * 66}, {w * 192, h * 66}, {w * 64, h * 194}}};
    const Point p{int64_t(x) * 256 + 128, int64_t(y) * 256 + 128};
    for (size_t i = 0; i < 3; ++i) {
        const auto& a = tri[i]; const auto& b = tri[(i + 1) % 3];
        const int64_t edge = (b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0]);
        if (!edge) throw std::runtime_error("Reference geometry has an ambiguous edge sample");
        if (edge < 0) return false;
    }
    return true;
}
inline Reference reference(const std::string& scene, Size size, uint32_t iteration) {
    if (scene == "texture-copy" || scene == "texture-sample") return {texture(size, iteration), size.width * size.height, 0};
    if (scene != "triangle" && scene != "alpha-blend") throw std::runtime_error("Unknown offscreen reference scene");
    Reference out;
    out.pixels.resize(size_t(size.width) * size.height * 4);
    const bool blend = scene == "alpha-blend";
    for (uint32_t y = 0; y < size.height; ++y) for (uint32_t x = 0; x < size.width; ++x) {
        Pixel pixel = background(iteration);
        const bool first = covered(size, x, y, 0), second = blend && covered(size, x, y, 1);
        if (first) pixel = shade(pixel, color(iteration, 0, blend), blend);
        if (second) pixel = shade(pixel, color(iteration, 1, blend), blend);
        out.covered += first || second; out.overlap += first && second;
        std::copy(pixel.begin(), pixel.end(), out.pixels.begin() + (size_t(y) * size.width + x) * 4);
    }
    if (!out.covered || (blend && !out.overlap)) throw std::runtime_error("Reference scene is empty / has no blend overlap");
    return out;
}
inline Difference compare(const std::vector<uint8_t>& actual, const std::vector<uint8_t>& expected, uint32_t tolerance) {
    if (actual.size() != expected.size() || actual.size() % 4) throw std::runtime_error("Invalid image comparison dimensions");
    Difference out;
    for (size_t p = 0; p < actual.size(); p += 4) {
        bool bad = false;
        for (size_t c = 0; c < 4; ++c) {
            const auto error = uint32_t(std::abs(int(actual[p + c]) - int(expected[p + c])));
            out.maxError = std::max(out.maxError, error);
            out.channels += error > tolerance; bad |= error > tolerance;
        }
        out.pixels += bad;
    }
    return out;
}
inline uint32_t guards(const uint8_t* bytes, size_t imageBytes) {
    uint32_t bad = 0;
    for (size_t i = 0; i < kGuardBytes; ++i) {
        bad += bytes[i] != kGuardValue;
        bad += bytes[kGuardBytes + imageBytes + i] != kGuardValue;
    }
    return bad;
}
inline void selfTest() {
    auto ref = reference("alpha-blend", {32, 32}, 0);
    if (ref.covered != 200 || ref.overlap != 72) throw std::runtime_error("Raster coverage / overlap regression");
    const auto odd = reference("alpha-blend", {65, 37}, 1), large = reference("alpha-blend", {127, 95}, 2);
    if (odd.covered != 443 || odd.overlap != 137 || large.covered != 2264 || large.overlap != 790)
        throw std::runtime_error("Odd-size raster coverage / overlap regression");
    if (shade({17, 29, 43, 255}, {0.75f, 0.25f, 0.5f, 0.5f}, true) != Pixel{104, 46, 85, 255})
        throw std::runtime_error("Blend reference regression");
    if (shade({17, 29, 43, 79}, {0.75f, 0.25f, 0.5f, 0.5f}, true) != Pixel{104, 46, 85, 167})
        throw std::runtime_error("Non-opaque alpha reference regression");
    auto actual = ref.pixels;
    if (compare(actual, ref.pixels, 1).pixels) throw std::runtime_error("Image verifier rejected reference");
    actual[0] += 1;
    if (compare(actual, ref.pixels, 1).pixels) throw std::runtime_error("Image verifier rejected allowed tolerance");
    actual[4] += 2;
    auto diff = compare(actual, ref.pixels, 1);
    if (diff.pixels != 1 || diff.channels != 1 || diff.maxError != 2) throw std::runtime_error("Image verifier missed corruption");
    if (!compare(actual, ref.pixels, 0).pixels) throw std::runtime_error("Texture verifier accepted byte corruption");
    std::vector<uint8_t> readback(ref.pixels.size() + 2 * kGuardBytes, kGuardValue);
    if (guards(readback.data(), ref.pixels.size())) throw std::runtime_error("Guard verifier rejected sentinel");
    readback[0] ^= 1; readback.back() ^= 1;
    if (guards(readback.data(), ref.pixels.size()) != 2) throw std::runtime_error("Guard verifier missed corruption");
}
} // namespace offscreen
