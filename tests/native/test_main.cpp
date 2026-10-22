#include "input.hpp"
#include "output.hpp"
#include "golden_vectors.hpp"
#include "dsp/sync_correlator.hpp"
#include "spear/dsp/channelizer.hpp"
#include "spear/dsp/fir.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numbers>
#include <random>
#include <stdexcept>

using namespace spear;
using namespace spear::std_t98;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(__func__) + ": " + #x); } while (0)

void protocol_vectors() {
    CHECK(golden::cases().size() == 40);
    for (const auto& c : golden::cases()) {
        auto f = parse_frame(dewhiten(c.symbols));
        const auto r = decode_rich(f.rich);
        CHECK(r.valid && r.f == c.rich_f && r.m == c.rich_m && r.d == c.rich_d);
        CHECK(r.parity == c.rich_parity && r.parity_ok == c.rich_parity_ok);
        const auto s = decode_sacch(f.sacch);
        CHECK(s.msg_type == c.sacch_msg_type && s.user_code == c.sacch_user && s.maker_code == c.sacch_maker);
        CHECK(s.call_stat == c.sacch_call && s.bit_errors == c.sacch_errors && s.crc_ok == c.sacch_crc_ok && s.crc_recv == c.sacch_crc_recv);
        const auto p = decode_pich(f.tch1);
        CHECK(p.csm == c.pich_csm && p.bit_errors == c.pich_errors && p.crc_ok == c.pich_crc_ok && p.crc_recv == c.pich_crc_recv);
        const auto payload = traffic_blocks_to_payload(split_traffic_blocks(f));
        const std::vector<float> raw(c.symbols.begin(), c.symbols.end());
        const auto llr = traffic_llr(raw);
        CHECK(payload.size() == 36 && llr.size() == 288);
        for (std::size_t i = 0; i < 288; ++i) CHECK((llr[i] > 0) == (((payload[i/8] >> (7-i%8)) & 1) != 0));
    }
}

void append_frame(std::vector<float>& s, bool sync = true) {
    if (sync) s.insert(s.end(), kSyncWord.begin(), kSyncWord.end());
    else s.insert(s.end(), 10, 0);
    for (int i = 0; i < 182; ++i) s.push_back(i % 2 ? 1.f : -1.f);
}
std::vector<uint64_t> sync_frames(std::vector<float> samples, bool crc, std::size_t chunk) {
    SyncCorrelator corr;
    std::vector<uint64_t> emitted;
    for (std::size_t p = 0; p < samples.size(); p += chunk)
        corr.process(std::span(samples).subspan(p, std::min(chunk, samples.size()-p)), p,
            [crc](const auto&) { return crc; },
            [&](uint64_t first, bool emit) { if (emit) emitted.push_back(first); });
    return emitted;
}
void synchronization() {
    std::vector<float> s(5, 0);
    append_frame(s);
    CHECK(sync_frames(s, false, 37).empty());
    CHECK(sync_frames(s, true, 37) == std::vector<uint64_t>{5});
    append_frame(s); append_frame(s, false); append_frame(s);
    s.insert(s.end(), 20, 0);
    const std::vector<uint64_t> expected{5,197,389,581};
    CHECK(sync_frames(s, false, 37) == expected);
    CHECK(sync_frames(s, false, s.size()) == expected);
    s.clear(); append_frame(s); append_frame(s);
    for (int i = 0; i < 3; ++i) append_frame(s, false);
    s.insert(s.end(), 20, 0);
    CHECK(sync_frames(s, false, 1) == (std::vector<uint64_t>{0,192}));
}

void channelizer() {
    for (int count : {48,64}) {
        const auto taps = dsp::design_lowpass(count * 6250., 3125, count * 8 - 1);
        dsp::PfbChannelizer a(count, count * 6250., taps), b(count, count * 6250., taps);
        std::vector<cf32> tone(count * 40);
        for (std::size_t i = 0; i < tone.size(); ++i) tone[i] = std::polar(1.f, static_cast<float>(2 * std::numbers::pi * 7 * i / count));
        std::vector<std::vector<cf32>> whole, split;
        a.process(tone, whole);
        for (std::size_t p = 0; p < tone.size(); p += 37) b.process(std::span(tone).subspan(p, std::min<std::size_t>(37,tone.size()-p)), split);
        CHECK(whole == split);
        std::size_t strongest = 0;
        for (std::size_t i = 1; i < whole.size(); ++i)
            if (std::norm(whole[i].back()) > std::norm(whole[strongest].back())) strongest = i;
        CHECK(strongest == 7);
        CHECK(std::abs(whole[7].back()) > .9f);
    }
}

void single_channel_only() {
    for (int count : {48, 64}) {
        auto taps = dsp::design_lowpass(count * 6250., 3125, count * 8 - 1);
        dsp::PfbChannelizer reference(count, count * 6250., taps);
        t98::CenterChannelizer selected(count, count * 6250., taps);
        std::mt19937 rng(98);
        std::uniform_real_distribution<float> random(-1, 1);
        std::vector<cf32> input(count * 100 + 17);
        for (auto& x : input) x = {random(rng), random(rng)};
        std::vector<std::vector<cf32>> full, one;
        reference.process(input, full);
        for (std::size_t pos = 0; pos < input.size(); pos += 37)
            selected.process(std::span(input).subspan(pos, std::min<std::size_t>(37, input.size()-pos)), one);
        CHECK(one.size() == 1 && one[0].size() == full[0].size());
        for (std::size_t i = 0; i < one[0].size(); ++i) CHECK(std::abs(one[0][i] - full[0][i]) < 1e-6f);
        CHECK(selected.info().group_delay_in == reference.info().group_delay_in);
    }
    ReceiverConfig cfg;
    CHECK(cfg.num_channels == 1);
    cfg.num_channels = 30;
    bool rejected = false;
    try { Receiver receiver(cfg); } catch (const std::invalid_argument&) { rejected = true; }
    CHECK(rejected);
}

void output_format() {
    std::vector<uint8_t> payload(36);
    for (int i = 0; i < 36; ++i) payload[i] = static_cast<uint8_t>(i);
    auto bytes = t98::encode_burst(payload);
    CHECK(bytes[0] == 0xff && bytes.size() == 41);
    for (int i = 0; i < 4; ++i) {
        CHECK(bytes[1+i*10] == 0x48);
        for (int j = 0; j < 9; ++j) CHECK(bytes[2+i*10+j] == i*9+j);
    }
    bool rejected = false;
    try { t98::encode_burst(std::span(payload).first(35)); } catch (const std::invalid_argument&) { rejected = true; }
    CHECK(rejected);
    const auto base = std::filesystem::current_path() / "test-output";
    for (auto ext : {".burst", ".ambe", ".llr"}) std::filesystem::remove(base.string()+ext);
    {
        t98::Output writer(base, true, true, false);
        Frame f; f.rich.valid = true; f.rich.f = 1; f.tch_payload = payload; f.tch_llr.assign(288, 12);
        writer.write(f);
        CHECK(writer.bursts() == 1);
        f.rich.f = 0; writer.write(f); CHECK(writer.bursts() == 1);
    }
    CHECK(std::filesystem::file_size(base.string()+".burst") == 41);
    CHECK(std::filesystem::file_size(base.string()+".ambe") == 40);
    CHECK(std::filesystem::file_size(base.string()+".llr") == 288);
    rejected = false;
    try { t98::Output writer(base, false, false, false); } catch (const std::runtime_error&) { rejected = true; }
    CHECK(rejected);
    for (auto ext : {".burst", ".ambe", ".llr"}) std::filesystem::remove(base.string()+ext);
}

void silence_input() {
    ReceiverConfig cfg; cfg.num_channels = 1;
    Receiver receiver(cfg);
    int frames = 0;
    Observer obs; obs.frame = [&](const auto&) { ++frames; };
    receiver.set_observer(std::move(obs));
    const std::vector<cf32> zeros(32768);
    for (uint64_t i = 0; i < 8; ++i) receiver.process(zeros, i*zeros.size());
    CHECK(frames == 0);
    CHECK(!receiver.metrics(0).locked);
}
int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--rx-taps") {
            const auto taps = make_rx_taps(62500, 2400, .2, 520);
            std::ofstream stream(argv[2], std::ios::binary);
            stream.write(reinterpret_cast<const char*>(taps.data()), taps.size() * sizeof(float));
            CHECK(stream.good());
            return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--fixture") {
            t98::Output writer(argv[2], true, true, false);
            Frame f; f.rich.valid = true; f.rich.f = 1;
            for (int i = 0; i < 36; ++i) f.tch_payload.push_back(static_cast<uint8_t>(i));
            for (int i = 0; i < 288; ++i) f.tch_llr.push_back((f.tch_payload[i/8] >> (7-i%8)) & 1 ? 60 : -60);
            for (int i = 0; i < 10; ++i) writer.write(f);
            return 0;
        }
        protocol_vectors(); synchronization(); channelizer(); single_channel_only(); output_format(); silence_input(); std::cout << "40 golden protocol vectors, sync/flywheel, single-channel filter equivalence, file format and receiver: passed\n";
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
