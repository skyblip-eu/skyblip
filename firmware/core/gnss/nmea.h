// core/gnss/nmea.h: GNSS NMEA-0183 line parser (L76K: GPS/GLONASS/BeiDou/QZSS).
#ifndef SKYBLIP_CORE_GNSS_NMEA_H
#define SKYBLIP_CORE_GNSS_NMEA_H

#include <cstdint>

namespace skyblip::gnss {

// INFO: gn 09Jun25 GGA field 11 is the geoid separation and plenty of receivers
// either omit it or answer "0.0,M" forever. OGN falls back to a manual parameter
// defaulting to 40 m (oss/nrf52-ogn-tracker src/main.h:38, src/gps.cpp:526-529);
// SoftRF special-cases the always-zero chipsets (oss/SoftRF-lyusupov
// .../src/driver/GNSS.cpp:1681-1684). Our operating region is central Europe,
// where EGM96 separation runs 45-48 m, so 46 m is the assumption we make and
// declare through geoid_separation_measured.
constexpr int32_t kDefaultGeoidSeparationM = 46;

// INFO: fc 03aug26 A GGA that stops mid-sentence still carries a checksum over
// what did arrive, so the checksum cannot catch it. moshe-braner refuses any GGA
// shorter than 40 characters and drops the fix with it (oss/SoftRF-moshe-braner
// .../src/driver/GNSS.cpp:2226-2236, `write_size > 40` else `badGGA`).
constexpr int kMinGgaLength = 40;

// INFO: fc 03aug26 An MTK-lineage receiver with no almanac reports a date in
// 1980 next to a position that looks perfectly ordinary; OGN refuses any
// two-digit year at or above 70 for that reason (oss/nrf52-ogn-tracker
// src/ogn.h:737-738, "MTK GPS can produce fake date with year 1980"). Every date
// this device will ever see is 20xx.
constexpr int kMaxTwoDigitYear = 70;

// Which sentence a parse consumed. The caller ages GGA and RMC separately, so it
// has to be told which one just arrived; a fix is not a fix on one of them.
enum class Sentence : uint8_t { None, Rmc, Gga, Txt };

struct GnssFix {
    bool valid{false};
    bool utc_valid{false};
    int32_t lat_1e7{0};
    int32_t lon_1e7{0};
    // INFO: gn 09Jun25 alt_m is height above the WGS-84 ELLIPSOID (HAE), which is
    // what ADS-L 4 SRD860 issue 2 G.1.7 transmits and what every received
    // neighbour altitude is measured in. alt_msl_m is GGA field 9, the value a
    // panel or an IGC file wants.
    int32_t alt_m{0};
    int32_t alt_msl_m{0};
    int32_t geoid_separation_m{kDefaultGeoidSeparationM};
    uint16_t speed_q{0};
    uint16_t track_c9{0};
    uint32_t utc{0};
    // Horizontal dilution of precision in hundredths, GGA field 8.
    uint16_t hdop_e2{0};
    // How late this solution reached us relative to the PPS edge it describes.
    // Zero until a driver that knows its part stamps it.
    uint16_t pps_latency_ms{0};
    uint8_t sats{0};
    uint8_t fix_quality{0};
    bool alt_msl_valid{false};
    bool alt_hae_valid{false};
    bool geoid_separation_measured{false};
    uint32_t updates{0};
};

class NmeaParser {
   public:
    bool feed(char c);

    bool parse_line(const char* line, int len);

    const GnssFix& fix() const { return fix_; }
    void reset() { pos_ = 0; }

    // Which sentence the last accepted parse was. Only meaningful right after
    // feed() or parse_line() returned true.
    Sentence last_sentence() const { return last_; }

    // The receiver's answer to $PCAS06, "$GPTXT,01,01,02,SW=<version>": empty
    // until the part has named itself. SoftRF takes the same substring and logs
    // it (oss/SoftRF-lyusupov .../src/driver/GNSS.cpp:1015-1025).
    const char* firmware_version() const { return version_; }
    bool identified() const { return version_[0] != 0; }

   private:
    static constexpr int kVersionCap = 24;

    char buf_[100];
    int pos_{0};
    GnssFix fix_;
    char version_[kVersionCap]{};
    Sentence last_{Sentence::None};

    bool apply_rmc(const char* fields[], int nf);
    bool apply_gga(const char* fields[], int nf, int len);
    bool apply_txt(const char* line, int len);
};

bool nmea_checksum_ok(const char* line, int len);
int32_t nmea_parse_coord(const char* dm, char hemi);

// When the solution in `fix` was actually true, given the millisecond its last
// sentence arrived. The burst always trails the second it describes; how far is
// a property of the receiver, so the part stamps it and this applies it.
inline uint32_t fix_instant_ms(const GnssFix& fix, uint32_t arrival_ms) {
    return arrival_ms - fix.pps_latency_ms;
}

}

#endif
