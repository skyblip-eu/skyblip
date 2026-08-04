#ifndef SKYBLIP_CORE_COMMS_CONFIG_H
#define SKYBLIP_CORE_COMMS_CONFIG_H

#include "core/comms/diagnostics.h"
#include "core/comms/timing_report.h"
#include "core/flight/state.h"
#include "core/messages/messages.h"
#include "core/power/battery.h"
#include "core/power/cutoff.h"
#include "core/power/reset_reason.h"
#include "core/settings/settings.h"
#include "core/timing/channel.h"
#include "core/timing/durable_write.h"
#include "core/timing/timing_stats.h"
#include "hal/dfu.h"
#include "hal/link.h"

namespace skyblip::comms {

enum class FlightState : uint8_t { Ground, Airborne, Unknown };

enum class Pending : uint8_t { None, Set, Dfu, Apply, Recovery, PowerOff, EraseLog, GnssCold };

// INFO: cf 02aug26 The gate reads whatever core/flight decided from the fix
// stream, and only the two codes it recognises mean anything: ADS-L G.1.4
// OnGround is the single value that can open this door. Every other code, and
// every value the enum does not name, is Unknown - which refuses.
FlightState flight_state_from(uint8_t adsl_code);

// What a prompt says it is, and what confirming it will do. The panel and the
// phone read the same two strings, so a pilot pressing the button and a pilot
// reading the app cannot be looking at two different descriptions of the same
// authorisation.
const char* pending_title(Pending pending);
const char* pending_detail(Pending pending);

// INFO: cf 02aug26 A prompt is an open authorisation standing on the glass, so
// it is not allowed to stand there forever: a device left on a wing with an
// unanswered "confirm firmware upload" is a device a stranger can walk up to.
// Long enough to put a phone down and reach the button, and no longer.
constexpr uint32_t kConfirmWindowMs = 30u * 1000u;

// INFO: cf 02aug26 A push exists so a tablet does not have to poll a gauge that
// barely moves between polls. Pushing on every millivolt reading would just
// move the polling onto the link instead of removing it, so a push is earned
// only by a step this wide, a charging flip, or a level change - three signals
// a pilot actually cares about, not the ADC's noise floor.
constexpr uint8_t kBatteryPushStepPercent = 5;

// INFO: fc 04aug26 The smallest ATT payload a single-frame reply is built to fit.
// An iOS central commonly settles at ATT_MTU 185, three of which are the
// notification header, so 182 is the narrowest real phone in the field and every
// reply that a pilot's app depends on is sized under it at its worst case rather
// than against a local buffer. hal::kMinimumLinkPayload (20) is lower still and
// is what BLE guarantees; a link that comes up there gets a counted refusal, not
// a notification the controller will fail.
constexpr int kSmallestSupportedPayload = 182;

class ConfigService {
   public:
    ConfigService(hal::Link& link, settings::Settings& s, hal::Dfu* dfu = nullptr,
                  const timing::SlotTimingStats* timing_stats = nullptr)
        : link_(link), settings_(s), dfu_(dfu), timing_stats_(timing_stats) {}

    void set_flight_state(FlightState fs);
    FlightState flight_state() const { return flight_; }

    // INFO: fc 03aug26 The RED technical file wants the clear-channel threshold
    // actually in force and the interval it is assessed over, and asks for a
    // test mode to read them. This is not a mode: the status reply the companion
    // link already answers on demand carries both, so a laboratory copies two
    // numbers out of a text frame instead of photographing an e-paper page whose
    // nine traffic rows are already full. EN 300 220-2 V3.3.1 §4.6.2.3, §4.6.3.2.
    void set_carrier_sense(int8_t threshold_dbm) { carrier_sense_dbm_ = threshold_dbm; }

    // The durable-write policy's own counters, read out beside the dwell
    // histograms because they are measurements of the same second: a write that
    // could not be placed inside its bound is a stall that landed where the slot
    // map did not budget for one, and it must not be silent. Wired by the product
    // that owns the write (products/skyblip_go/services/config.h).
    void set_durable_writes(const timing::DurableWriteWindow* writes) { writes_ = writes; }

    // Drives the upload-window timeout. Called once per App::step().
    void tick(uint32_t now_ms);

    // Consulted by the MCUmgr image-upload hook. An unauthenticated SMP
    // transport is what makes the browser update page possible (encrypted GATT
    // characteristics are unreliable under Web Bluetooth), so authorisation
    // lives here instead: an upload is only accepted inside a window that a
    // physical button press opened, while the device is positively on the
    // ground. Rogue firmware is stopped separately, by MCUboot's signature
    // check. This stops a stranger in range wasting the secondary slot.
    bool upload_allowed() const { return upload_window_open_ && on_ground(); }
    void close_upload_window() { upload_window_open_ = false; }

    void on_link_up(const messages::LinkUp& up);
    void on_link_down(const messages::LinkDown& down);
    void on_rx(const messages::RxFrame& frame);

    // Whether anyone is listening. Exported because it is the one fact about
    // this service that no reply reveals until the gauge happens to move, and a
    // product test has to be able to assert that a connection reached it.
    bool link_up() const { return link_up_; }
    uint16_t session() const { return session_; }

    // The values core/power already decided: state of charge, the millivolt
    // reading, whether the charger is holding the rail, and the level
    // core/power's cutoff rule made of the same samples. This service carries
    // them to the link, it does not re-derive what a low cell is. While a link
    // is up, a charging flip, a level change or a step-sized move in percent
    // pushes an unsolicited status so the tablet's gauge moves without a poll.
    void set_battery_state(const power::BatteryState& battery, power::PowerLevel level);

    // How warm the silicon is, in tenths of a degree, and whether anybody has
    // actually read one. Invalid is not zero: a board with no sensor, a driver
    // that refused a measurement and a device at freezing are three different
    // things, and 0.0 C is a plausible hangar morning - so an invalid reading
    // leaves the key out of the reply altogether rather than publishing a number
    // nobody measured.
    void set_die_temperature(int16_t decicelsius, bool valid) {
        diag_.die_decicelsius = decicelsius;
        diag_.die_valid = valid;
    }

    // The status dump, held here because this is the one service that can put it
    // on a link (core/comms/diagnostics.h). Everything above writes its own half
    // of it, so the reply, the unsolicited push and the console dump report one
    // set of numbers rather than three copies of them; the product's collector
    // fills the other half through this door, once a second.
    Diagnostics& diagnostics() { return diag_; }
    const Diagnostics& diagnostics() const { return diag_; }

    // Receptions core/traffic refused because the position they claimed was
    // further away than this radio can hear. The number a support case opens
    // with: it says whether the range gate fires once a week or once a second,
    // and the second one is a decoder or a link budget, not a sky full of
    // aircraft.
    void set_range_refused(uint32_t count) { diag_.range_refused = count; }

    // The nRF52840's power-failure comparator has fired, latched by core/power.
    // Kept beside the level because the two feed one rule
    // (core/power/cutoff.h::may_write) and this service is the door that rule
    // has to be enforced at.
    void set_supply_warned(bool warned) { supply_warned_ = warned; }

    // Whether a settings change may be accepted at all. A "set" refused here is
    // refused to the phone, with a reason, rather than accepted and quietly not
    // written: below the low-battery warning the settings sector is not touched,
    // and a companion app that patched a value per keystroke has to be told so.
    bool settings_writable() const {
        return power::may_write(diag_.level, supply_warned_, power::DurableWrite::Settings);
    }

    // INFO: cf 02aug26 BLE pairing is off on this product (encrypted GATT
    // characteristics break Web Bluetooth on Windows), so physical presence is
    // what stands in for it: nothing sensitive happens without a gesture made
    // on the device itself. These two are the only doors, and only ui/input's
    // confirmation gesture, the prompt timeout and a dropped link may knock.
    void confirm();
    void cancel();

    Pending pending() const { return pending_; }
    bool settings_dirty() const { return dirty_; }
    void clear_dirty() { dirty_ = false; }

    // INFO: cf 02aug26 The panel's settings page edits the same struct this
    // service was handed, one validated field at a time, and this is how it
    // says so. There is exactly one writer of the flash blob - the product's
    // ConfigLinkService, on this flag - so the two editors cannot disagree
    // about what is stored. Deliberately not gated on flight state: a "set"
    // from a phone is refused unless the device is positively on the ground,
    // because a phone in a pocket has no proven presence and rewrites the whole
    // struct in one message. A press on the panel is presence itself and moves
    // one field, and alarm volume is a thing a pilot needs in the air.
    void note_settings_changed() { dirty_ = true; }

    // Why the device came up. Read once at boot by the shell, reported here so
    // a watchdog bite in the field is diagnosable without the panel in hand.
    void set_reset_reason(power::ResetReason reason) { diag_.reset = reason; }
    power::ResetReason reset_reason() const { return diag_.reset; }

    // Erasing the flight log destroys evidence a pilot may need for a claim or
    // an incident, so it knocks on the same door a firmware upload does: the
    // flight log service asks, the panel shows the prompt, and only the
    // confirmation gesture on the device itself latches it.
    void request_log_erase();
    bool log_erase_requested() const { return log_erase_requested_; }
    void clear_log_erase_request() { log_erase_requested_ = false; }

    // Latched by a confirmed "gnss_cold". A receiver whose almanac is poisoned
    // takes twenty minutes to fix and a pilot reads that as a broken device, so
    // throwing the stored orbit data away has to be reachable without a cable.
    // It is behind the same confirmation as the rest: a cold start costs the next
    // fix, and a phone must not be able to spend that on its own.
    bool gnss_cold_start_requested() const { return gnss_cold_requested_; }
    void clear_gnss_cold_start_request() { gnss_cold_requested_ = false; }

    // Latched by a confirmed "power_off". The shell owns the sequencer, so this
    // is where the request waits for it: core/power::ShutdownSequencer::request
    // with ShutdownReason::LinkRequest.
    bool power_off_requested() const { return power_off_requested_; }
    void clear_power_off_request() { power_off_requested_ = false; }

    const char* pending_json() const { return pending_buf_; }

    // INFO: fc 04aug26 Every frame this service could not put on the link: one
    // that would not fit the negotiated payload, one the controller refused, and
    // one a writer left incomplete. Silence towards a phone is a fault worth a
    // number, and the pilot-facing push is the one path that also retries.
    uint32_t link_drops() const { return diag_.link_drops; }

   private:
    Status reply(const char* json);
    Status reply(const char* json, int len);
    int payload() const;
    void stage(Pending pending, const char* reason);
    void ack(bool ok, const char* reason);
    void send_status();
    // The bench's plug-in-and-read for G6: the same on_rx dispatch that
    // answers "get" and "status" answers "timing" from the one accumulator
    // core/timing::SlotTimingStats keeps, over the link the phone already has
    // open - no second channel, no panel real estate a histogram would not
    // fit on anyway.
    void send_timing();
    // The same door, one more question: what the durable-write policy did with the
    // settings changes it was handed (core/timing/durable_write.h).
    void send_flash();
    // And the radio's own half of the same bench: the dump's radio subsystem, on
    // its own, because a companion page that only draws the air picture should not
    // have to read the receiver's firmware string to get the noise floor. Not four
    // more keys on "status": that reply is the one this service PUSHES
    // unsolicited and is already sized against the narrowest phone in the field at
    // its worst case, with eleven bytes left.
    void send_radio();
    // And the whole dump, which is the same table as the console's: one frame per
    // subsystem where the payload allows it, more where it does not, and never a
    // frame that mixes two subsystems (core/comms/diagnostics.h).
    void send_diagnostics();
    void send_report(DiagnosticsReport& report);
    static const char* flight_name(FlightState fs);
    bool on_ground() const { return flight_ == FlightState::Ground; }

    hal::Link& link_;
    settings::Settings& settings_;
    hal::Dfu* dfu_;
    const timing::SlotTimingStats* timing_stats_;
    const timing::DurableWriteWindow* writes_{nullptr};
    FlightState flight_{FlightState::Unknown};
    int8_t carrier_sense_dbm_{timing::NoiseFloor::kSeedDbm + timing::NoiseFloor::kClearMarginDb};
    Diagnostics diag_{};
    bool supply_warned_{false};
    bool link_up_{false};
    bool status_push_due_{false};
    bool airborne_latched_{false};
    bool power_off_requested_{false};
    bool log_erase_requested_{false};
    bool gnss_cold_requested_{false};
    Pending pending_{Pending::None};
    bool dirty_{false};
    bool upload_window_open_{false};
    uint16_t session_{0};
    uint32_t now_ms_{0};
    uint32_t window_opened_ms_{0};
    uint32_t pending_since_ms_{0};
    char pending_buf_[256]{0};
    int pending_len_{0};

    // Long enough to upload ~730 KB over BLE on a slow phone, short enough that
    // a device left on a bench does not stay writable all afternoon.
    static constexpr uint32_t kUploadWindowMs = 10u * 60u * 1000u;
};

}

#endif
