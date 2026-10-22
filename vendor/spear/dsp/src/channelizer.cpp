#include "spear/dsp/channelizer.hpp"

#include <cmath>
#include <numbers>

namespace spear::dsp {

PfbChannelizer::PfbChannelizer(std::size_t num_channels, double in_rate, const std::vector<float>& prototype)
    : m_(num_channels), in_rate_(in_rate), proto_(prototype) {
    // prototype を M の倍数へパディング
    while (proto_.size() % m_ != 0) proto_.push_back(0.f);
    taps_per_branch_ = proto_.size() / m_;
    // polyphase 分解: branch m の t 番目 tap = h[t*M + m]
    branch_taps_.assign(m_, std::vector<float>(taps_per_branch_, 0.f));
    for (std::size_t t = 0; t < taps_per_branch_; ++t)
        for (std::size_t m = 0; m < m_; ++m)
            branch_taps_[m][t] = proto_[t * m_ + m];
    delay_.assign(m_, std::vector<cf32>(taps_per_branch_, cf32{0.f, 0.f}));
    buf_.resize(m_);
    roots_.resize(m_ * m_);
    for (std::size_t c = 0; c < m_; ++c)
        for (std::size_t k = 0; k < m_; ++k)
            roots_[c * m_ + k] = std::polar(1.0, 2 * std::numbers::pi * c * k / m_);
}

PfbChannelizer::~PfbChannelizer() = default;

StageInfo PfbChannelizer::info() const {
    StageInfo s;
    s.name = "pfb_channelizer";
    s.in_rate = in_rate_;
    s.out_rate = out_rate();
    s.group_delay_in = static_cast<double>(proto_.size() - 1) / 2.0;
    return s;
}

void PfbChannelizer::reset() {
    for (auto& d : delay_) std::fill(d.begin(), d.end(), cf32{0.f, 0.f});
    carry_.clear();
}

void PfbChannelizer::process(std::span<const cf32> in, std::vector<std::vector<cf32>>& outs) {
    if (outs.size() != m_) outs.assign(m_, {});
    // carry と in を連結して M サンプル単位で処理
    std::vector<cf32> data;
    data.reserve(carry_.size() + in.size());
    data.insert(data.end(), carry_.begin(), carry_.end());
    data.insert(data.end(), in.begin(), in.end());

    std::size_t pos = 0;
    for (; pos + m_ <= data.size(); pos += m_) {
        // commutator: 入力 j を branch (M-1-j) へ。各 branch の遅延線を1つ進める。
        for (std::size_t j = 0; j < m_; ++j) {
            const std::size_t br = m_ - 1 - j;
            auto& dl = delay_[br];
            // shift(新しいサンプルを先頭へ)
            for (std::size_t t = taps_per_branch_ - 1; t > 0; --t) dl[t] = dl[t - 1];
            dl[0] = data[pos + j];
        }
        // 各 branch の FIR 出力を FFT バッファへ
        for (std::size_t m = 0; m < m_; ++m) {
            cf32 acc{0.f, 0.f};
            const auto& dl = delay_[m];
            const auto& tp = branch_taps_[m];
            for (std::size_t t = 0; t < taps_per_branch_; ++t) acc += dl[t] * tp[t];
            buf_[m] = acc;
        }
        for (std::size_t c = 0; c < m_; ++c) {
            std::complex<double> sum{};
            for (std::size_t k = 0; k < m_; ++k)
                sum += std::complex<double>(buf_[k]) * roots_[c * m_ + k];
            outs[c].emplace_back(static_cast<float>(sum.real()), static_cast<float>(sum.imag()));
        }
    }
    carry_.assign(data.begin() + static_cast<long>(pos), data.end());
}

} // namespace spear::dsp
