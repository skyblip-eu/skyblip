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
