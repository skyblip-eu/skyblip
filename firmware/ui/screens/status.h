// Shows own-ship sensor state: GNSS fix, sats, position, altitude, speed, track,
// pressure, UTC, PPS lock, traffic count and the battery.
// This is the page that makes the sensor inputs observable on the panel.
#ifndef SKYBLIP_UI_SCREENS_STATUS_H
#define SKYBLIP_UI_SCREENS_STATUS_H

#include <cstdint>

#include "ui/framebuffer.h"

namespace skyblip::ui {

struct StatusSnapshot {
    uint32_t device_addr{0};
    // ADS-L carries no callsign, so this names the device on the glass and
    // nowhere else: which of three on a bench is the one in front of you.
    const char* callsign{""};
    bool fix_valid{false};
    bool utc_valid{false};
    bool pps_locked{false};
    bool baro_valid{false};
    uint8_t sats{0};
    int32_t lat_1e7{0};
    int32_t lon_1e7{0};
    int32_t alt_m{0};      // GNSS, WGS-84 ellipsoid
    int32_t alt_qnh_m{0};  // barometric altitude on the QNH set below
    int32_t alt_std_m{0};  // pressure altitude, 1013.25 hPa datum (QNE)
    uint32_t pressure_pa{0};
    uint32_t qnh_pa{0};    // the altimeter subscale setting
    uint16_t speed_q{0};   // quarter-m/s
    uint16_t track_c9{0};  // cordic9
    int16_t climb_e8{0};   // eighth-m/s
    uint32_t utc{0};
    int n_targets{0};
    bool battery_valid{false};
    bool charging{false};
    bool battery_low{false};
    uint16_t battery_mv{0};
    uint8_t battery_percent{0};
};

void draw_status(Framebuffer& fb, const StatusSnapshot& snap);

}  // namespace skyblip::ui

#endif
