#include "input.hpp"
#include "dashboard.hpp"
#include "output.hpp"
#include "receiver.hpp"
#include <chrono>
#include <cmath>
#include <csignal>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace {
volatile std::sig_atomic_t stopping = 0;
void stop(int) { stopping = 1; }
constexpr double first_channel_hz = 351200000;
void help() {
    std::cout << R"(std-t98-receiver 0.2.0 - RTL-SDR to STD-T98 .burst

  std-t98 --channel 1 --output capture
  std-t98 --channel 1 --output capture --ambe --llr
  std-t98 --input recording.cf32 --sample-rate 1200000 --center-freq 351293750 --channel 1

Options:
  --channel N          Registered channel 1..30 (default 1)
  -o, --output BASE    Output basename (default output; writes BASE.burst)
  --ambe               Also write legacy BASE.ambe (no burst markers)
  --llr                Also write signed soft decisions, BASE.llr (288 bytes/burst)
  --overwrite          Allow replacing existing output files
  --device N           RTL-SDR device index (default 0)
  --gain auto|DB       Tuner gain (default auto; manual uses nearest supported gain)
  --ppm N             RTL-SDR oscillator correction in ppm (default 0)
  --freq-error HZ      Residual signal offset; positive shifts DSP tuning upward
  --seconds N          Stop after N seconds of input samples
  --sample-rate HZ     Input rate (default 1200000; e.g. 2400000)
  --pfb 64|48          SPEAR PFB size (default 64; 48 for legacy comparisons)
  --center-freq HZ     IQ center frequency (live default: selected channel - 250 kHz)
  --input FILE         Replay little-endian complex float32 IQ; requires explicit
                       --sample-rate and --center-freq; no SDR library needed
  --rtl-library PATH   Explicit path to rtlsdr.dll / librtlsdr.so
  --list-devices       List RTL-SDR devices and exit
  --help               Show this help
  --version            Show version

Ctrl+C stops reception and closes the output. No audio or secret cracking runs here.
Interactive terminals show a fixed frame dashboard; redirected logs use text lines.
)";
}
double number(const std::string& text, const std::string& option) {
    std::size_t used = 0;
    double result;
    try { result = std::stod(text, &used); }
    catch (...) { throw std::invalid_argument("Invalid value for " + option + ": " + text); }
    if (used != text.size() || !std::isfinite(result)) throw std::invalid_argument("Invalid value for " + option);
    return result;
}
int integer(const std::string& text, const std::string& option, int low, int high) {
    const double n = number(text, option);
    if (n < low || n > high || std::floor(n) != n) throw std::invalid_argument(option + " is outside its integer range");
    return static_cast<int>(n);
}
}

int main(int argc, char** argv) {
    try {
#ifdef _WIN32
        // CRT narrow argv uses the active ANSI code page. Preserve Japanese paths.
        int wide_count = 0;
        auto wide = CommandLineToArgvW(GetCommandLineW(), &wide_count);
        if (!wide) throw std::runtime_error("Cannot read command line");
        std::vector<std::string> utf8;
        for (int i = 0; i < wide_count; ++i) {
            int n = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
            std::string value(n, '\0');
            WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, value.data(), n, nullptr, nullptr);
            value.pop_back();
            utf8.push_back(std::move(value));
        }
        LocalFree(wide);
        std::vector<char*> pointers;
        for (auto& s : utf8) pointers.push_back(s.data());
        argc = wide_count;
        argv = pointers.data();
#endif
        t98::RadioOptions radio;
        int channel = 1, pfb = 64;
        std::string input, output = "output";
        double center = 0, error = 0, seconds = 0;
        bool ambe = false, llr = false, overwrite = false, list = false;
        bool rate_given = false, center_given = false;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            auto value = [&]() -> std::string { if (++i >= argc) throw std::invalid_argument("Missing value for " + arg); return argv[i]; };
            if (arg == "--help" || arg == "-h") { help(); return 0; }
            else if (arg == "--version") { std::cout << "std-t98-receiver 0.2.0\n"; return 0; }
            else if (arg == "--channel") channel = integer(value(), arg, 1, 30);
            else if (arg == "--device") radio.device = integer(value(), arg, 0, 255);
            else if (arg == "--pfb") { pfb = integer(value(), arg, 48, 64); if (pfb != 48 && pfb != 64) throw std::invalid_argument("--pfb must be 64 or 48"); }
            else if (arg == "--output" || arg == "-o") output = value();
            else if (arg == "--input") input = value();
            else if (arg == "--rtl-library") radio.library = value();
            else if (arg == "--sample-rate") { radio.rate = integer(value(), arg, 300000, 10000000); rate_given = true; }
            else if (arg == "--center-freq") { center = number(value(), arg); center_given = true; }
            else if (arg == "--freq-error") error = number(value(), arg);
            else if (arg == "--ppm") radio.ppm = integer(value(), arg, -1000, 1000);
            else if (arg == "--seconds") { seconds = number(value(), arg); if (seconds <= 0 || seconds > 86400) throw std::invalid_argument("--seconds must be in (0, 86400]"); }
            else if (arg == "--gain") { auto gain = value(); radio.automatic_gain = gain == "auto"; if (!radio.automatic_gain) { radio.gain_db = number(gain, arg); if (radio.gain_db < 0 || radio.gain_db > 60) throw std::invalid_argument("--gain must be auto or 0..60 dB"); } }
            else if (arg == "--ambe") ambe = true;
            else if (arg == "--llr") llr = true;
            else if (arg == "--overwrite") overwrite = true;
            else if (arg == "--list-devices") list = true;
            else throw std::invalid_argument("Unknown option: " + arg + " (see --help)");
        }
        if (list) { t98::list_rtl(radio.library); return 0; }
        if (output.empty()) throw std::invalid_argument("--output must not be empty");
        if (!input.empty() && (!rate_given || !center_given)) throw std::invalid_argument("IQ replay requires --sample-rate and --center-freq");
        if (!input.empty() && radio.ppm != 0) throw std::invalid_argument("--ppm applies to hardware only; use --freq-error for recordings");
        const double frequency = first_channel_hz + (channel - 1) * 6250;
        if (!center_given) center = frequency - 250000;
        if (center < 1 || center > 2000000000 || std::floor(center) != center) throw std::invalid_argument("--center-freq must be an integer Hz value in 1..2000000000");
        radio.frequency = static_cast<uint32_t>(center);
        spear::std_t98::ReceiverConfig cfg;
        cfg.in_rate = radio.rate;
        cfg.num_channels = 1; // Same SPEAR stages, only the selected channel is demodulated.
        cfg.pfb_channels = pfb;
        if (radio.rate % (pfb * 6250) != 0)
            throw std::invalid_argument("Rate must be a multiple of 400000 Hz for --pfb 64, or 300000 Hz for --pfb 48");
        cfg.freq_err_hz = frequency - center + error;
        if (std::abs(cfg.freq_err_hz) + 6250 >= radio.rate / 2.0) throw std::invalid_argument("Selected channel is outside the input IQ bandwidth");
        std::signal(SIGINT, stop);
        std::signal(SIGTERM, stop);
        auto source = input.empty() ? t98::open_rtl(radio) : t98::open_iq_file(std::filesystem::path(reinterpret_cast<const char8_t*>(input.c_str())));
        t98::Output writer(std::filesystem::path(reinterpret_cast<const char8_t*>(output.c_str())), ambe, llr, overwrite);
        spear::std_t98::Receiver receiver(cfg);
        std::cerr << "CH " << channel << " (" << std::fixed << std::setprecision(5) << frequency / 1e6 << " MHz), " << radio.rate << " samples/s\n";
        std::cerr << "SPEAR frontend: single selected channel, polyphase " << pfb << ", band rate " << pfb * 6250 << " samples/s\n";
        std::cerr << "Writing " << output << ".burst" << (ambe ? " + .ambe" : "") << (llr ? " + .llr" : "") << ". Ctrl+C to stop.\n";
        t98::Dashboard dashboard(channel, frequency, radio.rate, output, ambe, llr);
        auto last_report = std::chrono::steady_clock::now();
        bool drew_frame = false;
        spear::std_t98::Observer observer;
        observer.frame = [&](const auto& frame) {
            writer.write(frame);
            dashboard.on_frame(frame, receiver.metrics(0), writer.bursts());
            drew_frame = true;
            last_report = std::chrono::steady_clock::now();
        };
        receiver.set_observer(std::move(observer));
        dashboard.refresh(receiver.metrics(0), writer.bursts());
        std::vector<spear::cf32> block(8192);
        uint64_t index = 0;
        const uint64_t limit = seconds > 0 ? static_cast<uint64_t>(seconds * radio.rate) : std::numeric_limits<uint64_t>::max();
        bool last_locked = false;
        while (!stopping && index < limit) {
            auto n = source->read(block, [] { return stopping != 0; });
            if (!n) break;
            n = static_cast<std::size_t>(std::min<uint64_t>(n, limit - index));
            drew_frame = false;
            receiver.process(std::span(block).first(n), index);
            index += n;
            const auto& m = receiver.metrics(0);
            if ((!drew_frame && m.locked != last_locked) ||
                std::chrono::steady_clock::now() - last_report >= dashboard.refresh_interval()) {
                dashboard.refresh(m, writer.bursts());
                last_report = std::chrono::steady_clock::now();
            }
            last_locked = m.locked;
        }
        dashboard.close();
        std::cerr << "Saved " << writer.bursts() << " bursts (" << index << " IQ samples).\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
