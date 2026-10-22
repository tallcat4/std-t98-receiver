#include "rrc.hpp"

#include <cmath>
#include <complex>
#include <mutex>
#include <numbers>
#include <vector>

namespace spear::std_t98 {

namespace {
double bessel_i0(double x) {
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 60; ++k) {
        term *= (x / 2.0) / k;
        sum += term * term;
        if (term * term < 1e-16 * sum) break;
    }
    return sum;
}
std::vector<std::complex<double>> ifft(const std::vector<std::complex<double>>& X) {
    // The filter designer always requests a power-of-two length. Portable
    // radix-2 inverse FFT replaces FFTW; normalization and spectrum are unchanged.
    const std::size_t N = X.size();
    auto x = X;
    for (std::size_t i = 1, j = 0; i < N; ++i) {
        std::size_t bit = N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(x[i], x[j]);
    }
    for (std::size_t len = 2; len <= N; len <<= 1) {
        const auto step = std::polar(1.0, 2 * std::numbers::pi / len);
        for (std::size_t i = 0; i < N; i += len) {
            std::complex<double> w{1, 0};
            for (std::size_t j = 0; j < len/2; ++j) {
                const auto a = x[i+j], b = x[i+j+len/2] * w;
                x[i+j] = a+b; x[i+j+len/2] = a-b; w *= step;
            }
        }
    }
    for (auto& v : x) v /= static_cast<double>(N);
    return x;
}
} // namespace

namespace { enum class Shape { Tx, Rx, Rrc }; std::vector<float> design_impl(double fs, double sym_rate, double alpha, int ntaps, Shape shape); }
std::vector<float> design_taps(double fs, double sym_rate, double alpha, int ntaps, bool rx) { return design_impl(fs, sym_rate, alpha, ntaps, rx ? Shape::Rx : Shape::Tx); }
std::vector<float> make_rrc_taps(double fs, double sym_rate, double alpha, int ntaps) { return design_impl(fs, sym_rate, alpha, ntaps, Shape::Rrc); }
namespace {
std::vector<float> design_impl(double fs, double sym_rate, double alpha, int ntaps, Shape shape) {
    const double T = 1.0 / sym_rate;
    int p = 0;
    while ((1 << p) < std::max(2048, 8 * ntaps)) ++p;
    const int fft_len = 1 << p;
    // f = fftshift(fftfreq(fft_len, 1/fs)) → S[i] for f_i = (i - fft_len/2) * fs / fft_len
    std::vector<double> S(fft_len);
    const double f0 = (1.0 - alpha) / (2.0 * T), f1 = (1.0 + alpha) / (2.0 * T);
    for (int i = 0; i < fft_len; ++i) {
        const double f = (i - fft_len / 2) * fs / fft_len;
        const double af = std::fabs(f);
        double H = 0;
        if (af < f0) H = 1.0;
        else if (af <= f1) H = std::cos((T / (4.0 * alpha)) * (2.0 * std::numbers::pi * af - std::numbers::pi * (1.0 - alpha) / T));
        const double x = f * T;
        const double P = x == 0.0 ? 1.0 : std::sin(std::numbers::pi * x) / (std::numbers::pi * x);
        S[i] = shape == Shape::Rx ? H / std::max(std::fabs(P), 1e-8) : shape == Shape::Tx ? H * P : std::sqrt(std::max(0.0, H));
    }
    // ifftshift → ifft → real → fftshift
    std::vector<std::complex<double>> X(fft_len);
    for (int i = 0; i < fft_len; ++i) X[i] = S[(i + fft_len / 2) % fft_len];
    auto h = ifft(X);
    std::vector<double> hc(fft_len);
    for (int i = 0; i < fft_len; ++i) hc[i] = h[(i + fft_len / 2) % fft_len].real();
    const int mid = fft_len / 2, half = ntaps / 2;
    std::vector<double> taps;
    if (ntaps % 2 == 1) taps.assign(hc.begin() + mid - half, hc.begin() + mid + half + 1);
    else taps.assign(hc.begin() + mid - half, hc.begin() + mid + half);
    // Kaiser β=8
    const int N = static_cast<int>(taps.size());
    const double beta = 8.0, i0b = bessel_i0(beta);
    double g = 0;
    for (int n = 0; n < N; ++n) {
        const double r = 2.0 * n / (N - 1) - 1.0;
        taps[n] *= bessel_i0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0b;
        g += taps[n];
    }
    std::vector<float> out(N);
    for (int n = 0; n < N; ++n) out[n] = static_cast<float>(g != 0 ? taps[n] / g : taps[n]);
    return out;
}
} // namespace

} // namespace spear::std_t98
