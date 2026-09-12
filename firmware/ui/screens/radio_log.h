#ifndef SKYBLIP_UI_SCREENS_RADIO_LOG_H
#define SKYBLIP_UI_SCREENS_RADIO_LOG_H

#include <cstdint>

#include "core/radio/log.h"
#include "ui/framebuffer.h"

namespace skyblip::ui {

constexpr int kRadioLogRows = radio::Log::kCapacity;

struct GnssReception {
    bool fix_valid{false};
    bool utc_valid{false};
    bool pps_locked{false};
    uint8_t sats{0};
    uint16_t hdop_e2{0};
    uint32_t solutions{0};
};

struct RadioLogSnapshot {
    GnssReception gnss{};
    uint32_t rx_ok{0};
    uint32_t tx_ok{0};
    int n_rows{0};
    const radio::Log* log{nullptr};
};

void draw_radio_log(Framebuffer& fb, const RadioLogSnapshot& snap);

}  // namespace skyblip::ui

#endif
