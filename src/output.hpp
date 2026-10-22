#pragma once
#include "receiver.hpp"
#include <array>
#include <filesystem>
#include <fstream>
#include <span>

namespace t98 {
std::array<uint8_t, 41> encode_burst(std::span<const uint8_t> payload);
class Output {
    std::ofstream burst_, ambe_, llr_;
    uint64_t bursts_ = 0;
public:
    Output(const std::filesystem::path& base, bool ambe, bool llr, bool overwrite);
    void write(const spear::std_t98::Frame& frame);
    uint64_t bursts() const { return bursts_; }
};
}
