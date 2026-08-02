// core/gnss/nmea.h: GNSS NMEA-0183 line parser (L76K: GPS/GLONASS/BeiDou/QZSS).
#ifndef SKYBLIP_CORE_GNSS_NMEA_H
#define SKYBLIP_CORE_GNSS_NMEA_H

#include <cstdint>

namespace skyblip::gnss {

struct GnssFix {
    bool valid{false};
    bool utc_valid{false};
    int32_t lat_1e7{0};
    int32_t lon_1e7{0};
    int32_t alt_m{0};
    uint16_t speed_q{0};
    uint16_t track_c9{0};
    uint32_t utc{0};
    uint8_t sats{0};
    uint8_t fix_quality{0};
    uint32_t updates{0};
};

class NmeaParser {
   public:
    bool feed(char c);

    bool parse_line(const char* line, int len);

    const GnssFix& fix() const { return fix_; }
    void reset() { pos_ = 0; }

   private:
    char buf_[100];
    int pos_{0};
    GnssFix fix_;

    bool apply_rmc(const char* fields[], int nf);
    bool apply_gga(const char* fields[], int nf);
};

bool nmea_checksum_ok(const char* line, int len);
int32_t nmea_parse_coord(const char* dm, char hemi);

}

#endif
