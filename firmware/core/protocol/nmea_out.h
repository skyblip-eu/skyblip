// core/protocol/nmea_out.h: ALP-TAS (FLARM-wire-compatible) NMEA output for
#ifndef SKYBLIP_CORE_PROTOCOL_NMEA_OUT_H
#define SKYBLIP_CORE_PROTOCOL_NMEA_OUT_H

#include <cstddef>
#include <cstdint>

#include "core/messages/messages.h"

namespace skyblip::protocol {

int nmea_finish(char* sentence, int body_len);

uint8_t adsl_cat_to_alptas(uint8_t adsl_cat);
uint8_t addr_table_to_idtype(uint8_t addr_table);

int format_pflaa(char* out, size_t cap, const messages::OwnState& own,
                 const messages::AircraftObs& t, uint8_t alarm_level);

// rel_bearing_deg is SIGNED, half a turn either way: the field is, and a value
// pushed through an unsigned parameter puts a threat off the left wing at 340
// degrees on an app that reads it as written.
int format_pflau(char* out, size_t cap, const messages::OwnState& own, int n_targets,
                 const messages::AircraftObs* threat, uint8_t alarm_level, int16_t rel_bearing_deg,
                 int32_t rel_vert_m, int32_t rel_dist_m);

// fix_valid follows the same binary own.fix_valid this device already keeps
// (no 2D/3D distinction survives to OwnState), which is what format_pflau's own
// GPS field already reduces the same fix to: '3' (3D) when true, '1' (no fix)
// when false. Both SoftRF forks send the same two values off the same test
// (src/protocol/data/NMEA.cpp: isValidGNSSFix() ? '3' : '1').
int format_pgrmz(char* out, size_t cap, int32_t alt_ft, bool fix_valid);

// $LK8EX1: pressure, pressure altitude, vertical speed, temperature and the
// battery, in the one sentence LK8000, XCSoar and their descendants already
// parse. Every field has a value that means "not available", which is why each
// one is clamped short of its own sentinel below: a real reading that happened
// to land on the sentinel would be read as no reading at all.
constexpr uint32_t kLk8NoPressurePa = 999999;
constexpr int32_t kLk8NoAltitudeM = 99999;
constexpr int32_t kLk8NoVarioCmS = 9999;
constexpr int32_t kLk8NoTemperatureC = 99;
constexpr uint32_t kLk8NoBattery = 999;

// Field 5 is one field carrying two different quantities, and which one it is
// is decided by magnitude alone: below 1000 it is a VOLTAGE in volts, at 1000
// and above it is a PERCENTAGE with 1000 added, so 1000 is 0% and 1100 is 100%.
// We send the percentage form. SoftRF's lyusupov tree sends the voltage form
// (src/protocol/data/NMEA.cpp:246-256, "$LK8EX1,999999,%d,%d,%d,%s" with a
// "4.1"-style string), and the moshe-braner fork corrected itself to the
// percentage with the reason in the code - "LK8000 specs say send percent
// instead of volts as an integer, percent+1000" (MB src/driver/Baro.cpp:406-407,
// and again at src/protocol/data/NMEA.cpp:1399-1400); GXAirCom sends
// Clamp(percent,0,100)+1000 (src/main.cpp:4382). The percentage is also the
// figure our own panel draws, and a device that says 38% while the tablet says
// something else is a support call. Sending 38 in the voltage half of this field
// would put 38 volts in front of a pilot.
constexpr uint32_t kLk8BatteryPercentBase = 1000;

// SoftRF's own bounds on the two fields it clamps, kept because they are what
// the consumers have been fed for years: altitude -1000..99998 m and temperature
// -99..98 C (lyusupov NMEA.cpp:250-253). The other two are one short of their
// sentinel for the same reason.
constexpr uint32_t kLk8MaxPressurePa = kLk8NoPressurePa - 1;
constexpr int32_t kLk8MinAltitudeM = -1000;
constexpr int32_t kLk8MaxAltitudeM = kLk8NoAltitudeM - 1;
constexpr int32_t kLk8MaxVarioCmS = kLk8NoVarioCmS - 1;
constexpr int32_t kLk8MinVarioCmS = -kLk8MaxVarioCmS;
constexpr int32_t kLk8MinTemperatureC = -99;
constexpr int32_t kLk8MaxTemperatureC = kLk8NoTemperatureC - 1;

// What the device has to say, each part of it separately present or absent: a
// unit with no barometer still has a battery, and that alone is a sentence worth
// sending (MB sends exactly that, "$LK8EX1,999999,99999,9999,99,%d", when no
// baro chip answered).
struct Lk8Ex1 {
    uint32_t pressure_pa{0};
    int32_t alt_m{0};
    int32_t vario_cm_s{0};
    int32_t temperature_c{0};
    uint8_t battery_percent{0};
    bool has_pressure{false};
    bool has_alt{false};
    bool has_vario{false};
    bool has_temperature{false};
    bool has_battery{false};
};

int format_lk8ex1(char* out, size_t cap, const Lk8Ex1& v);

// $GPRMC/$GPGGA: ownship's own absolute position, for the EFB that has no GNSS
// of its own (a panel-mounted tablet with no sky view). Neither is formatted
// without both own.fix_valid and own.utc_valid: there is no position to report
// without the first, and no way to stamp it without the second, and a sentence
// missing both a real fix and a real time is worse than silence to a consumer
// that stamps a log with it.
int format_gprmc(char* out, size_t cap, const messages::OwnState& own);
int format_gpgga(char* out, size_t cap, const messages::OwnState& own);

bool relative_ned(const messages::OwnState& own, const messages::AircraftObs& t, int32_t& north_m,
                  int32_t& east_m, int32_t& up_m);

}

#endif
