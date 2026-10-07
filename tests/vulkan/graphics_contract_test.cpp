// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "graphics_shader_contract.h"
#include "offscreen_reference.h"
#include <filesystem>
#include <fstream>
#include <iostream>

std::vector<uint32_t> read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const auto length = file.tellg();
    if (!file || length < 20 || length > 16 * 1024 * 1024 || length % 4) throw std::runtime_error("Invalid shader file");
    std::vector<uint32_t> words(size_t(length) / 4); file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(words.data()), length)) throw std::runtime_error("Cannot read shader");
    return words;
}
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--reference") { offscreen::selfTest(); return 0; }
        if (argc < 3 || argc > 4) throw std::runtime_error("usage: graphics_contract_test DIRECTORY metal-air|glsl-control [--mutations]");
        const std::string origin = argv[2];
        if (origin != "metal-air" && origin != "glsl-control") throw std::runtime_error("Unknown graphics shader origin");
        const bool metal = origin == "metal-air", mutations = argc == 4 && std::string(argv[3]) == "--mutations";
        if (argc == 4 && !mutations) throw std::runtime_error("Unknown test option");
        for (size_t index = 0; index < offscreen::shaderNames.size(); ++index) {
            auto words = read(std::filesystem::path(argv[1]) / (std::string(offscreen::shaderNames[index]) + ".spv"));
            const bool int8 = offscreen::shaderContract(words, index, metal);
            if (mutations) {
                auto refused = [&](const std::vector<uint32_t>& bad) {
                    try { offscreen::shaderContract(bad, index, metal); }
                    catch (const std::exception&) { return; }
                    throw std::runtime_error("Contract accepted injected corruption");
                };
                auto bad = words; bad[5] = 0; refused(bad);
                bad = words; bad.push_back((2u << 16) | 17); bad.push_back(5302); refused(bad); // bindless capability
                for (size_t i = 5; i < words.size(); i += words[i] >> 16) {
                    const auto op = words[i] & 0xffff;
                    if (op == 15) { bad = words; bad[i + 1] = 5; refused(bad); }
                    if (op == 71 && words[i] >> 16 == 4 && words[i + 2] == 33) {
                        bad = words; bad[i + 3] ^= 1; refused(bad);
                    }
                }
            }
            std::cout << offscreen::shaderNames[index] << ": PASS; shaderInt8=" << int8 << '\n';
        }
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
