#include "dashboard.hpp"
#include <algorithm>
#include <bitset>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string_view>
#include <vector>
#ifndef _WIN32
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace t98 {
namespace {
std::string bits_or_dash(const std::string& bits) { return bits.empty() ? "-" : bits; }
std::string fit(std::string value, std::size_t width) {
    if (value.size() > width) {
        if (width > 3) {
            std::size_t end = width - 3;
            while (end && (static_cast<unsigned char>(value[end]) & 0xc0) == 0x80) --end;
            value.resize(end);
            value += "...";
        } else value.resize(width);
    }
    value.append(width - value.size(), ' ');
    return value;
}
std::string hex_bits(std::string_view bits) {
    if (bits.empty() || bits.size() % 4) return "-";
    constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(bits.size() / 4);
    for (std::size_t i = 0; i < bits.size(); i += 4) {
        unsigned value = 0;
        for (std::size_t j = 0; j < 4; ++j) value = (value << 1) | (bits[i+j] == '1');
        result.push_back(hex[value]);
    }
    return result;
}
const char* call_state(int state) {
    return state == 0 ? "Normal" : state == 1 ? "Secret" : "Unknown";
}
}

Dashboard::Dashboard(int channel, double frequency_hz, uint32_t sample_rate,
                     std::string output, bool ambe, bool llr)
    : channel_(channel), frequency_hz_(frequency_hz), sample_rate_(sample_rate),
      output_(std::move(output)), ambe_(ambe), llr_(llr) {
#ifdef _WIN32
    console_handle_ = GetStdHandle(STD_ERROR_HANDLE);
    if (console_handle_ != INVALID_HANDLE_VALUE && console_handle_ &&
        GetConsoleMode(console_handle_, &original_mode_)) {
        terminal_ = true;
        screen_ = SetConsoleMode(console_handle_, original_mode_ | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
    }
#else
    terminal_ = isatty(STDERR_FILENO);
    screen_ = terminal_;
#endif
    if (screen_) std::cerr << "\x1b[?1049h\x1b[?25l\x1b[2J" << std::flush;
}

Dashboard::~Dashboard() { close(); }

void Dashboard::close() {
    if (closed_) return;
    if (screen_) std::cerr << "\x1b[?25h\x1b[?1049l" << std::flush;
    else if (terminal_ && previous_width_) std::cerr << '\n';
#ifdef _WIN32
    if (terminal_ && console_handle_ != INVALID_HANDLE_VALUE) SetConsoleMode(console_handle_, original_mode_);
#endif
    closed_ = true;
}

void Dashboard::on_frame(const spear::std_t98::Frame& frame,
                         const spear::std_t98::ChannelMetrics& metrics, uint64_t bursts) {
    if (have_frame_ && frame.input_sample_index > last_sample_) {
        const double dt = 1000.0 * (frame.input_sample_index - last_sample_) / sample_rate_;
        if (dt < 1000.0) {
            average_dt_ms_ = have_dt_ ? 0.8 * average_dt_ms_ + 0.2 * dt : dt;
            have_dt_ = true;
        }
    }
    last_sample_ = frame.input_sample_index;
    last_frame_ = frame;
    have_frame_ = true;
    ++frames_;
    if (frame.rich.valid && frame.rich.f == 0) {
        last_pich_ = frame.pich;
        have_pich_ = true;
        if (frame.pich.crc_ok) { ++crc_ok_; csm_ = frame.pich.csm; }
    } else if (frame.rich.valid && frame.rich.f == 1) {
        last_sacch_ = frame.sacch;
        have_sacch_ = true;
        if (frame.sacch.crc_ok) ++crc_ok_;
    }
    paint(metrics, bursts, true);
}

void Dashboard::refresh(const spear::std_t98::ChannelMetrics& metrics, uint64_t bursts) {
    paint(metrics, bursts, false);
}

std::string Dashboard::summary(bool synced, double power_db, uint64_t bursts) const {
    std::ostringstream line;
    line << "RX: [" << (synced ? "OPEN " : "CLOSE") << "]  "
         << "Bursts: " << bursts << "  Frames: " << frames_
         << "  CRC: " << crc_ok_ << '/' << frames_
         << "  Power: " << std::showpos << std::fixed << std::setprecision(1) << power_db << " dB";
    return line.str();
}

void Dashboard::paint(const spear::std_t98::ChannelMetrics& metrics, uint64_t bursts, bool frame_event) {
    const bool synced = frame_event || metrics.locked;
    if (!screen_) {
        const auto value = summary(synced, metrics.power_db, bursts) +
            "  CSM: " + csm_ +
            (have_sacch_ && last_sacch_.crc_ok ? "  Call: " + std::string(call_state(last_sacch_.call_stat)) : "");
        if (terminal_) {
            std::cerr << '\r' << value;
            if (value.size() < previous_width_) std::cerr << std::string(previous_width_ - value.size(), ' ');
            std::cerr << std::flush;
            previous_width_ = value.size();
        } else {
            if (frame_event || !have_log_status_ || synced != last_log_synced_ || !frames_)
                std::cerr << value << '\n';
            have_log_status_ = true;
            last_log_synced_ = synced;
        }
        return;
    }

    int width = 80, height = 24;
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (GetConsoleScreenBufferInfo(console_handle_, &info)) {
        width = info.srWindow.Right - info.srWindow.Left + 1;
        height = info.srWindow.Bottom - info.srWindow.Top + 1;
    }
#else
    winsize size{};
    if (ioctl(STDERR_FILENO, TIOCGWINSZ, &size) == 0) {
        if (size.ws_col) width = size.ws_col;
        if (size.ws_row) height = size.ws_row;
    }
#endif
    struct Row { std::string text; const char* color; };
    std::vector<Row> lines;
    const bool compact = width < 80 || height < 22;
    const std::size_t table_width = static_cast<std::size_t>(std::max(6, std::min(width - 1, 180)));
    const std::string border = "+" + std::string(table_width - 2, '-') + "+";
    constexpr auto dim = "\x1b[90m";
    constexpr auto normal = "\x1b[37m";
    constexpr auto cyan = "\x1b[1;36m";
    constexpr auto green = "\x1b[1;32m";
    constexpr auto yellow = "\x1b[1;33m";
    constexpr auto red = "\x1b[1;31m";
    auto add = [&](std::string value, const char* color = nullptr) {
        lines.push_back({"| " + fit(std::move(value), table_width - 4) + " |", color ? color : normal});
    };
    auto divider = [&] { lines.push_back({border, dim}); };
    std::ostringstream title;
    title << "STD-T98 RECEIVER    CH " << std::setfill('0') << std::setw(2) << channel_
          << "    " << std::fixed << std::setprecision(5) << frequency_hz_ / 1e6 << " MHz";
    std::ostringstream state;
    state << "RX " << (synced ? "[ OPEN ]" : "[ WAIT ]")
          << "    Power " << std::showpos << std::fixed << std::setprecision(1)
          << metrics.power_db << " dB" << std::noshowpos;
    std::ostringstream counts;
    counts << "Bursts " << bursts << "    Frames " << frames_
           << "    CRC " << crc_ok_ << '/' << frames_ << "    dt ";
    if (have_dt_) counts << std::fixed << std::setprecision(1) << average_dt_ms_ << " ms";
    else counts << "-- ms";
    std::string pich = "PICH   CSM " + csm_ + "    CRC " +
                       (have_pich_ ? (last_pich_.crc_ok ? "OK" : "NG") : "--") +
                       "    Errors " + (have_pich_ ? std::to_string(last_pich_.bit_errors) : "--");
    std::string sacch = "SACCH  ";
    if (have_sacch_) {
        sacch += "UC " + (last_sacch_.crc_ok ? std::to_string(last_sacch_.user_code) : "--") +
                 "    Maker " + (last_sacch_.crc_ok ? std::to_string(last_sacch_.maker_code) : "--") +
                 "    Call " + (last_sacch_.crc_ok ? call_state(last_sacch_.call_stat) : "Unknown") +
                 "    CRC " + (last_sacch_.crc_ok ? "OK" : "NG") +
                 "    Errors " + std::to_string(last_sacch_.bit_errors);
    } else sacch += "Waiting...";
    auto tch = [&](const char* name, const std::string& bits) {
        const bool binary = table_width >= 170 && bits.size() == 144;
        return std::string(name) + "   " +
               (bits.empty() ? "Waiting..." : binary ? bits : hex_bits(bits)) +
               (bits.empty() ? "" : binary ? " [bits]" : " [hex]");
    };
    divider();
    add(title.str(), cyan);
    divider();
    add(state.str(), synced ? green : yellow);
    add(counts.str(), frames_ && crc_ok_ < frames_ ? yellow : normal);
    if (compact) {
        add(pich, have_pich_ && last_pich_.crc_ok ? green : yellow);
        add(sacch, have_sacch_ ? (last_sacch_.crc_ok
            ? (last_sacch_.call_stat == 1 ? yellow : green) : red) : dim);
        add(tch("TCH1", have_frame_ ? last_frame_.fields.tch1 : std::string()));
        add(tch("TCH2", have_frame_ ? last_frame_.fields.tch2 : std::string()));
        add("Output  " + output_ + ".burst", dim);
        add("Ctrl+C to stop and save capture.", dim);
        divider();
    } else {
        add("Output  " + output_ + ".burst" + (ambe_ ? " + .ambe" : "") + (llr_ ? " + .llr" : ""), dim);
        divider();
        add("CONTROL CHANNELS", cyan);
        add("SW     " + (have_frame_ ? bits_or_dash(last_frame_.fields.sw) : "Waiting...") +
            "    " + (have_frame_ ? (last_frame_.sync_ok ? "OK" : "Flywheel") : "--"),
            have_frame_ && last_frame_.sync_ok ? green : yellow);
        add("RICH   " + (have_frame_ ? bits_or_dash(last_frame_.fields.rich) : "Waiting..."));
        if (have_frame_ && last_frame_.rich.valid) {
            const auto& r = last_frame_.rich;
            add("       F " + std::to_string(r.f) + (r.f == 0 ? " Sync" : " Traffic") +
                "    Res " + std::to_string(r.res1) + ',' + std::to_string(r.res2) +
                "    M " + std::bitset<3>(r.m).to_string() + "    D " + std::to_string(r.d) +
                "    Parity " + (r.parity_ok ? "OK" : "NG"), r.parity_ok ? green : red);
        } else add("       F --    Res --    M ---    D --    Parity --", dim);
        add("SACCH  " + (have_frame_ ? bits_or_dash(last_frame_.fields.sacch) : "Waiting..."));
        if (have_sacch_) {
            const auto& s = last_sacch_;
            add("       F " + std::to_string(s.f) + "    Wr " + std::bitset<2>(s.wr).to_string() +
                "    Msg " + std::bitset<5>(s.msg_type).to_string() +
                "    CRC " + (s.crc_ok ? "OK" : "NG") +
                "    Recv " + s.crc_recv + " / Calc " + s.crc_calc,
                s.crc_ok ? green : red);
        } else {
            add("       F --    Wr --    Msg -----    CRC --", dim);
        }
        add(sacch, have_sacch_ ? (last_sacch_.crc_ok
            ? (last_sacch_.call_stat == 1 ? yellow : green) : red) : dim);
        add(pich, have_pich_ && last_pich_.crc_ok ? green : yellow);
        divider();
        add("TRAFFIC PAYLOAD  (144 bits per TCH)", cyan);
        add(tch("TCH1", have_frame_ ? last_frame_.fields.tch1 : std::string()));
        add(tch("TCH2", have_frame_ ? last_frame_.fields.tch2 : std::string()));
        divider();
        add("Ctrl+C to stop and save capture.  " + std::to_string(sample_rate_) + " samples/s", dim);
        divider();
    }
    std::cerr << "\x1b[H";
    for (std::size_t i = 0; i < lines.size() && i < static_cast<std::size_t>(height); ++i) {
        const auto limit = static_cast<std::size_t>(std::max(1, width - 1));
        if (lines[i].text.size() > limit) lines[i].text.resize(limit);
        std::cerr << lines[i].color << lines[i].text << "\x1b[0m\x1b[K";
        if (i + 1 < lines.size() && i + 1 < static_cast<std::size_t>(height)) std::cerr << '\n';
    }
    std::cerr << "\x1b[J" << std::flush;
}
}
