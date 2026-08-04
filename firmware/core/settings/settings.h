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

// The most the radio's centre frequency may be trimmed, in tenths of a ppm, and
// the reason there is a trim at all. The board fits a TCXO, so zero is the
// design intent and SoftRF hard-codes exactly that for the SX1262; OGN keeps the
// correction because it also runs on crystal parts (src/parameters.h:52-54). What
// a TCXO does not give us is a way to find out: there is no measurement on the
// device, so if a batch of references comes in ten times worse than its
// datasheet, a field with no way to correct it is a recall. The bound is that
// tenfold miss and no more (+-10 ppm, +-8.7 kHz at 868.2 MHz): inside it the
// carrier cannot leave ERC 70-03 band h1.4, and beyond it the part is broken
// rather than out of trim. sx::kFreqTrimLimitTenthsPpm is the same number where
// the PLL word is computed.
constexpr int16_t kFreqTrimLimitTenthsPpm = 100;

struct Settings {
    uint8_t version{kCurrentVersion};
    uint32_t device_addr{0};
    // The per-unit battery trim, core/power/battery.h's kCalibrationLimitMv
    // either way, applied to every reading before the gauge or the cutoff sees
    // it. Placed here rather than at the end because a signed 16-bit value after
    // the 32-bit address costs no padding, so the blob is the same length it was.
    //
    // It is the one field the "config" reply does not carry back. That reply is
    // one frame at core/comms/config.h's kSmallestSupportedPayload, its worst
    // case is already 173 of those 182 bytes, and no honest name for this key
    // fits in nine. It is written on the line and read back as the millivolts
    // the status reply and the panel already show, which is the figure the line
    // is comparing against a bench supply anyway.
    int16_t battery_offset_mv{0};
    // The radio's frequency trim in tenths of a ppm, positive upwards, applied
    // to every dwell the radio arms (hardware/parts/sx1262). Beside
    // battery_offset_mv because the pair of 16-bit trims fills one word with no
    // padding, and because they are the same kind of field: a per-unit
    // correction set on a bench, not a pilot's setting.
    //
    // Like battery_offset_mv it is accepted on "set" and absent from the "get"
    // reply, for the same reason: that reply is one frame at
    // core/comms/config.h's kSmallestSupportedPayload and its worst case has nine
    // bytes left. Unlike the battery trim there is nothing on the device that
    // reads the value back, so a bench that sets it has to trust its own record
    // and its spectrum analyser - which is the instrument that had to be there
    // to know the number in the first place.
    int16_t freq_trim_e1_ppm{0};
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
// power_save between aircraft_type and callsign; version 2 dropped those three
// and had no battery_offset_mv. from_blob still reads all of them. Version 3 is
// the same length as version 2 and a different layout, which is exactly the case
// a length check cannot catch and the version byte must. Version 4 adds
// freq_trim_e1_ppm and is four bytes longer than version 3.
constexpr uint8_t kBlobVersion = 4;

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
