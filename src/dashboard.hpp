#pragma once
#include "receiver.hpp"
#include <chrono>
#include <cstdint>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace t98 {
class Dashboard {
public:
    Dashboard(int channel, double frequency_hz, uint32_t sample_rate,
              std::string output, bool ambe, bool llr);
    ~Dashboard();
    Dashboard(const Dashboard&) = delete;
    Dashboard& operator=(const Dashboard&) = delete;

    void on_frame(const spear::std_t98::Frame& frame,
                  const spear::std_t98::ChannelMetrics& metrics, uint64_t bursts);
    void refresh(const spear::std_t98::ChannelMetrics& metrics, uint64_t bursts);
    std::chrono::milliseconds refresh_interval() const {
        return terminal_ ? std::chrono::milliseconds(100) : std::chrono::milliseconds(1000);
    }
    void close();
private:
    int channel_;
    double frequency_hz_;
    uint32_t sample_rate_;
    std::string output_;
    bool ambe_, llr_, terminal_ = false, screen_ = false, closed_ = false;
    uint64_t frames_ = 0, crc_ok_ = 0, last_sample_ = 0;
    double average_dt_ms_ = 0;
    bool have_dt_ = false, have_frame_ = false, have_pich_ = false, have_sacch_ = false;
    bool have_log_status_ = false, last_log_synced_ = false;
    spear::std_t98::Frame last_frame_;
    spear::std_t98::Pich last_pich_;
    spear::std_t98::Sacch last_sacch_;
    std::string csm_ = "Waiting...";
    std::size_t previous_width_ = 0;
#ifdef _WIN32
    HANDLE console_handle_ = INVALID_HANDLE_VALUE;
    DWORD original_mode_ = 0;
#endif
    void paint(const spear::std_t98::ChannelMetrics& metrics, uint64_t bursts, bool frame_event);
    std::string summary(bool synced, double power_db, uint64_t bursts) const;
};
}
