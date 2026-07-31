#include "simulator/world/scenario.h"

#include <cstdio>
#include <cstring>

#include "core/util/json_min.h"

namespace skyblip::simulator {

namespace {

// Split "[{...},{...}]" into its object substrings. The flat reader in
// core/util/json_min then handles each object, so nothing here re-implements
// number or string parsing.
std::vector<std::string> objects_in_array(const std::string& text, const char* key) {
    std::vector<std::string> out;
    const std::string needle = std::string("\"") + key + "\"";
    size_t at = text.find(needle);
    if (at == std::string::npos) return out;
    at = text.find('[', at);
    if (at == std::string::npos) return out;

    int depth = 0;
    size_t start = 0;
    for (size_t i = at; i < text.size(); i++) {
        const char c = text[i];
        if (c == ']' && depth == 0) break;
        if (c == '{') {
            if (depth == 0) start = i;
            depth++;
        } else if (c == '}') {
            depth--;
            if (depth == 0) out.push_back(text.substr(start, i - start + 1));
        }
    }
    return out;
}

long int_or(const json::Reader& r, const char* key, long fallback) {
    long v = 0;
    return r.get_int(key, v) ? v : fallback;
}

bool event_from(const json::Reader& r, ScenarioEvent& out) {
    struct Named {
        const char* key;
        EventKind kind;
    };
    static const Named kKeys[] = {
        {"fix", EventKind::Fix},
        {"pps", EventKind::Pps},
        {"baro", EventKind::Baro},
        {"battery_mv", EventKind::BatteryMv},
        {"external_power", EventKind::ExternalPower},
        {"button", EventKind::Button},
        {"altitude_m", EventKind::Altitude},
        {"track_deg", EventKind::Track},
        {"aircraft", EventKind::Aircraft},
        {"expect_alarm_min", EventKind::ExpectAlarmMin},
        {"expect_traffic_min", EventKind::ExpectTrafficMin},
    };
    out.at_ms = static_cast<uint32_t>(int_or(r, "at_ms", 0));
    for (const Named& n : kKeys) {
        if (!r.has(n.key)) continue;
        out.kind = n.kind;
        out.value = int_or(r, n.key, 1);
        return true;
    }
    return false;
}

protocol::System system_from(const std::string& name) {
    return name == "alptas" ? protocol::System::Alptas : protocol::System::AdslDirect;
}

Mode mode_from(const std::string& name) {
    if (name == "demo") return Mode::Demo;
    if (name == "training") return Mode::Training;
    return Mode::Dev;
}

}  // namespace

bool parse_scenario(const char* json, int len, Scenario& out) {
    if (json == nullptr || len <= 0) return false;
    const std::string text(json, static_cast<size_t>(len));
    const json::Reader top(json, len);

    char buf[64] = {0};
    if (top.get_str("name", buf, sizeof(buf))) out.name = buf;
    if (top.get_str("mode", buf, sizeof(buf))) out.mode = mode_from(buf);

    out.lat_1e7 = static_cast<int32_t>(int_or(top, "lat_1e7", out.lat_1e7));
    out.lon_1e7 = static_cast<int32_t>(int_or(top, "lon_1e7", out.lon_1e7));
    out.alt_m = static_cast<int32_t>(int_or(top, "alt_m", out.alt_m));
    out.speed_kt = static_cast<int32_t>(int_or(top, "speed_kt", out.speed_kt));
    out.track_deg = static_cast<int32_t>(int_or(top, "track_deg", out.track_deg));
    out.duration_ms = static_cast<uint32_t>(int_or(top, "duration_ms", out.duration_ms));

    for (const std::string& obj : objects_in_array(text, "aircraft")) {
        const json::Reader r(obj.c_str(), static_cast<int>(obj.size()));
        ScenarioAircraft a;
        if (r.get_str("system", buf, sizeof(buf))) a.system = system_from(buf);
        a.north_m = static_cast<double>(int_or(r, "north_m", 0));
        a.east_m = static_cast<double>(int_or(r, "east_m", 0));
        a.up_m = static_cast<double>(int_or(r, "up_m", 0));
        a.speed_mps = static_cast<double>(int_or(r, "speed_mps", 30));
        a.track_deg = static_cast<double>(int_or(r, "track_deg", 270));
        a.phase_ms = static_cast<int>(int_or(r, "phase_ms", -1));
        a.slot = static_cast<int>(int_or(r, "slot", -1));
        out.aircraft.push_back(a);
    }

    for (const std::string& obj : objects_in_array(text, "events")) {
        const json::Reader r(obj.c_str(), static_cast<int>(obj.size()));
        ScenarioEvent e;
        if (event_from(r, e)) out.events.push_back(e);
    }
    return true;
}

bool load_scenario(const char* path, Scenario& out) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::string text;
    char chunk[512];
    size_t n = 0;
    while ((n = std::fread(chunk, 1, sizeof(chunk), f)) > 0) text.append(chunk, n);
    std::fclose(f);
    return parse_scenario(text.c_str(), static_cast<int>(text.size()), out);
}

}  // namespace skyblip::simulator
