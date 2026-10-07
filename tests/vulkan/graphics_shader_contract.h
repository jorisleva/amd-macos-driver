// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace offscreen {
inline constexpr std::array<const char*, 4> shaderNames{"fullscreen.vert", "texture.frag", "triangle.vert", "solid.frag"};
// Admission for our two fixed test profiles, not a replacement for spirv-val.
// No Vulkan calls: also usable by the software-only contract tests on the Mac.
inline bool shaderContract(const std::vector<uint32_t>& words, size_t index, bool metal) {
    if (index >= shaderNames.size() || words.size() < 5 || words[0] != 0x07230203 ||
        words[1] > 0x00010500 || !words[3] || words[4]) throw std::runtime_error("Invalid graphics SPIR-V header");
    struct Type { uint32_t op; std::vector<uint32_t> operands; };
    std::map<uint32_t, Type> types;
    std::map<uint32_t, uint32_t> sets, bindings, storage, variableTypes;
    std::map<uint32_t, std::map<uint32_t, uint32_t>> offsets;
    std::set<uint32_t> blocks, readonly;
    bool int8 = false, push = false;
    size_t entries = 0;
    for (size_t i = 5; i < words.size();) {
        const uint32_t length = words[i] >> 16, op = words[i] & 0xffff;
        if (!length || length > words.size() - i) throw std::runtime_error("Malformed graphics SPIR-V instruction");
        auto at = [&](size_t n) { if (n >= length) throw std::runtime_error("Truncated graphics SPIR-V operand"); return words[i + n]; };
        if (op == 17) {
            const auto cap = at(1);
            if (length != 2 || (cap != 1 && cap != 50 && !(metal && index == 1 && cap == 39)))
                throw std::runtime_error("Capability outside graphics profile (bindless is unsupported)");
            int8 |= cap == 39;
        } else if (op == 15) {
            const auto model = at(1);
            if (length < 4 || model != ((index == 0 || index == 2) ? 0u : 4u)) throw std::runtime_error("Wrong graphics execution model");
            const char* name = reinterpret_cast<const char*>(&words[i + 3]);
            const char* end = std::find(name, name + (length - 3) * 4, '\0');
            if (end == name + (length - 3) * 4 || std::string(name, end) != "main") throw std::runtime_error("Graphics entry must be main");
            ++entries;
        } else if (op == 71) {
            const auto id = at(1), decoration = at(2);
            if (decoration == 34) sets[id] = at(3);
            if (decoration == 33) bindings[id] = at(3);
            if (decoration == 2) blocks.insert(id);
            if (decoration == 24) readonly.insert(id);
        } else if (op == 72 && length == 5 && at(3) == 35) {
            offsets[at(1)][at(2)] = at(4);
        } else if (op >= 19 && op <= 32) {
            types.emplace(at(1), Type{op, {words.begin() + i + 2, words.begin() + i + length}});
        } else if (op == 59) {
            variableTypes[at(2)] = at(1); storage[at(2)] = at(3); push |= at(3) == 9;
        }
        i += length;
    }
    if (entries != 1 || sets.size() != bindings.size() || (metal && push)) throw std::runtime_error("Graphics entry/descriptor/push-constant mismatch");
    const std::set<std::pair<uint32_t, uint32_t>> expected = index == 0 ? std::set<std::pair<uint32_t, uint32_t>>{}
        : index == 1 ? (metal ? std::set<std::pair<uint32_t, uint32_t>>{{0, 32}, {0, 160}} : std::set<std::pair<uint32_t, uint32_t>>{{0, 0}})
        : metal ? std::set<std::pair<uint32_t, uint32_t>>{{0, 0}} : std::set<std::pair<uint32_t, uint32_t>>{};
    std::set<std::pair<uint32_t, uint32_t>> actual;
    auto type = [&](uint32_t id, uint32_t op) -> const std::vector<uint32_t>& {
        auto found = types.find(id);
        if (found == types.end() || found->second.op != op) throw std::runtime_error("Graphics descriptor type mismatch");
        return found->second.operands;
    };
    for (auto [id, binding] : bindings) {
        if (!sets.count(id) || !storage.count(id) || !actual.emplace(sets.at(id), binding).second) throw std::runtime_error("Duplicate/missing graphics descriptor");
        const auto& pointer = type(variableTypes.at(id), 32);
        if (pointer.size() != 2 || pointer[0] != storage.at(id)) throw std::runtime_error("Graphics pointer storage mismatch");
        if (index == 1) {
            if (storage.at(id) != 0) throw std::runtime_error("Texture/sampler must be UniformConstant");
            uint32_t image = pointer[1];
            if (metal && binding == 160) { if (!type(image, 26).empty()) throw std::runtime_error("Invalid sampler type"); continue; }
            if (!metal) { const auto& combined = type(image, 27); if (combined.size() != 1) throw std::runtime_error("Invalid combined sampler type"); image = combined[0]; }
            const auto& shape = type(image, 25);
            if (shape.size() != 7 || type(shape[0], 22) != std::vector<uint32_t>{32} || shape[1] != 1 ||
                shape[2] || shape[3] || shape[4] || shape[5] != 1 || shape[6]) throw std::runtime_error("Expected sampled float32 2D nonarray image");
        } else {
            const auto& members = type(pointer[1], 30);
            if (storage.at(id) != 12 || !blocks.count(pointer[1]) || !readonly.count(id) || members.size() != 5 ||
                offsets[pointer[1]] != std::map<uint32_t, uint32_t>{{0, 0}, {1, 16}, {2, 20}, {3, 24}, {4, 28}})
                throw std::runtime_error("DrawParams must be a readonly 32-byte storage block");
            const auto& color = type(members[0], 23);
            if (color.size() != 2 || color[1] != 4 || type(color[0], 22) != std::vector<uint32_t>{32}) throw std::runtime_error("DrawParams color must be float4");
            for (size_t field = 1; field < members.size(); ++field)
                if (type(members[field], 21) != std::vector<uint32_t>{32, 0}) throw std::runtime_error("DrawParams fields must be uint32");
        }
    }
    if (actual != expected || (!metal && (index == 2 || index == 3) && !push)) throw std::runtime_error("Graphics descriptors outside selected ABI");
    return int8;
}
} // namespace offscreen
