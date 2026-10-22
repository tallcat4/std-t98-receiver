#include "output.hpp"
#include <algorithm>
#include <stdexcept>

namespace t98 {
std::array<uint8_t, 41> encode_burst(std::span<const uint8_t> payload) {
    if (payload.size() != 36) throw std::invalid_argument("A traffic burst must have 4 x 9 bytes");
    std::array<uint8_t, 41> result{};
    result[0] = 0xff;
    for (std::size_t i = 0; i < 4; ++i) {
        result[1 + i*10] = 0x48;
        std::copy_n(payload.begin() + i*9, 9, result.begin() + 2 + i*10);
    }
    return result;
}
Output::Output(const std::filesystem::path& base, bool ambe, bool llr, bool overwrite) {
    auto suffix = [&](const char* ext) { auto path = base; path += ext; return path; };
    std::vector<std::pair<std::ofstream*, std::filesystem::path>> files{{&burst_, suffix(".burst")}};
    if (ambe) files.push_back({&ambe_, suffix(".ambe")});
    if (llr) files.push_back({&llr_, suffix(".llr")});
    for (const auto& [stream, path] : files)
        if (!overwrite && std::filesystem::exists(path)) throw std::runtime_error("Output exists: " + path.string() + ". Choose another --output or use --overwrite.");
    if (!base.parent_path().empty()) std::filesystem::create_directories(base.parent_path());
    for (const auto& [stream, path] : files) {
        stream->open(path, std::ios::binary | std::ios::trunc);
        if (!*stream) throw std::runtime_error("Cannot create output: " + path.string());
        stream->exceptions(std::ios::badbit | std::ios::failbit);
    }
}
void Output::write(const spear::std_t98::Frame& frame) {
    if (!frame.rich.valid || frame.rich.f != 1 || frame.tch_payload.size() != 36) return;
    const auto bytes = encode_burst(frame.tch_payload);
    burst_.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    burst_.flush();
    if (ambe_.is_open()) {
        ambe_.write(reinterpret_cast<const char*>(bytes.data()+1), bytes.size()-1);
        ambe_.flush();
    }
    if (llr_.is_open()) {
        if (frame.tch_llr.size() != 288) throw std::runtime_error("Missing soft decisions for traffic burst");
        llr_.write(reinterpret_cast<const char*>(frame.tch_llr.data()), frame.tch_llr.size());
        llr_.flush();
    }
    ++bursts_;
}
}
