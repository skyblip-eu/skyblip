// core/protocol/nmea_out.h — ALP-TAS (FLARM-wire-compatible) NMEA output for
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

int format_pflau(char* out, size_t cap, const messages::OwnState& own, int n_targets,
                 const messages::AircraftObs* threat, uint8_t alarm_level, uint16_t rel_bearing_deg,
                 int32_t rel_vert_m, int32_t rel_dist_m);

int format_pgrmz(char* out, size_t cap, int32_t alt_ft);

bool relative_ned(const messages::OwnState& own, const messages::AircraftObs& t, int32_t& north_m,
                  int32_t& east_m, int32_t& up_m);

}

#endif
