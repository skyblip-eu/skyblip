// core/settings/settings.h: the config struct a pilot can change, its defaults,
// and the two framings it survives in: a versioned CRC blob for flash, JSON for
// the companion link. Validation is one function, so no path can store a value
// another path would refuse.
//
// Every field here has a reader outside this module. A page that accepts a
// setting the firmware ignores is worse than one that does not offer it, so a
// field that loses its last reader leaves the struct, the JSON and the schema
// together, and the blob layout is versioned so a device that stored the old
// one still comes back up as itself.
#ifndef SKYBLIP_CORE_SETTINGS_SETTINGS_H
#define SKYBLIP_CORE_SETTINGS_SETTINGS_H

#include <cstddef>
#include <cstdint>

#include "core/util/json_min.h"
#include "core/util/result.h"

namespace skyblip::settings {

enum class Units : uint8_t { Metric = 0, Imperial = 1 };

constexpr size_t kCallsignCap = 10;

struct Settings {
    uint8_t version{kCurrentVersion};
    uint32_t device_addr{0};
    uint8_t addr_table{0};
    uint8_t aircraft_type{4};
    bool alarm_enabled{true};
    uint8_t alarm_volume{3};
    bool stealth{false};
    Units units{Units::Metric};
    uint8_t page_mask{0x0F};
    char callsign[kCallsignCap]{0};

    // The companion-link contract, which is what schemas/config.v1.schema.json
    // pins. It moves when a key changes meaning, not when the flash layout does.
    static constexpr uint8_t kCurrentVersion = 1;
};

// The flash framing, which is a different thing from the contract above: it
// moves whenever the struct's bytes move. Version 1 carried region, rotation and
// power_save between aircraft_type and callsign; from_blob still reads it.
constexpr uint8_t kBlobVersion = 2;

Settings defaults(uint32_t addr = 0);

Status validate(const Settings& s);

size_t blob_size();
void to_blob(const Settings& s, uint8_t* out, size_t cap);
Status from_blob(const uint8_t* in, size_t len, Settings& out);

// INFO: fc 04aug26 The fields alone, into a writer the caller opened, so the
// companion link's "config" reply can tag them and stay one flat object. Nesting
// them as an escaped string cost 53 bytes of backslashes on a 158-byte body,
// which is what pushed that reply past what an iPhone will carry.
void write_json_fields(json::Writer& w, const Settings& s);

int to_json(const Settings& s, char* buf, int cap);
Status apply_json(Settings& s, const char* json, int len);

}  // namespace skyblip::settings

#endif
