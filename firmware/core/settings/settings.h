// core/settings/settings.h — the plain, versioned config struct + defaults +
#ifndef SKYBLIP_CORE_SETTINGS_SETTINGS_H
#define SKYBLIP_CORE_SETTINGS_SETTINGS_H

#include <cstddef>
#include <cstdint>

#include "core/util/result.h"

namespace skyblip::settings {

enum class Units : uint8_t { Metric = 0, Imperial = 1 };

struct Settings {
    uint8_t version{kCurrentVersion};
    uint32_t device_addr{0};
    uint8_t addr_table{0};
    uint8_t aircraft_type{4};
    uint8_t region{0};
    bool alarm_enabled{true};
    uint8_t alarm_volume{3};
    bool stealth{false};
    Units units{Units::Metric};
    uint8_t rotation{0};
    uint8_t page_mask{0x07};
    bool power_save{false};
    char callsign[10]{0};

    static constexpr uint8_t kCurrentVersion = 1;
};

Settings defaults(uint32_t addr = 0);

Status validate(const Settings& s);

size_t blob_size();
void to_blob(const Settings& s, uint8_t* out, size_t cap);
Status from_blob(const uint8_t* in, size_t len, Settings& out);

int to_json(const Settings& s, char* buf, int cap);
Status apply_json(Settings& s, const char* json, int len);

}

#endif
