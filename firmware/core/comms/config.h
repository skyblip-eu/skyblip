#ifndef SKYBLIP_CORE_COMMS_CONFIG_H
#define SKYBLIP_CORE_COMMS_CONFIG_H

#include "core/messages/messages.h"
#include "core/power/reset_reason.h"
#include "core/settings/settings.h"
#include "hal/dfu.h"
#include "hal/link.h"

namespace skyblip::comms {

enum class FlightState : uint8_t { Ground, Airborne, Unknown };

enum class Pending : uint8_t { None, Set, Dfu, Apply, Recovery, PowerOff };

class ConfigService {
   public:
    ConfigService(hal::Link& link, settings::Settings& s, hal::Dfu* dfu = nullptr)
        : link_(link), settings_(s), dfu_(dfu) {}

    void set_flight_state(FlightState fs);
    FlightState flight_state() const { return flight_; }

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

    void confirm();
    void cancel();

    Pending pending() const { return pending_; }
    bool settings_dirty() const { return dirty_; }
    void clear_dirty() { dirty_ = false; }

    // Why the device came up. Read once at boot by the shell, reported here so
    // a watchdog bite in the field is diagnosable without the panel in hand.
    void set_reset_reason(power::ResetReason reason) { reset_reason_ = reason; }
    power::ResetReason reset_reason() const { return reset_reason_; }

    // Latched by a confirmed "power_off". The shell owns the sequencer, so this
    // is where the request waits for it: core/power::ShutdownSequencer::request
    // with ShutdownReason::LinkRequest.
    bool power_off_requested() const { return power_off_requested_; }
    void clear_power_off_request() { power_off_requested_ = false; }

    const char* pending_json() const { return pending_buf_; }

   private:
    void reply(const char* json);
    void ack(bool ok, const char* reason);
    void send_status();
    static const char* flight_name(FlightState fs);
    bool on_ground() const { return flight_ == FlightState::Ground; }

    hal::Link& link_;
    settings::Settings& settings_;
    hal::Dfu* dfu_;
    FlightState flight_{FlightState::Unknown};
    power::ResetReason reset_reason_{power::ResetReason::Unknown};
    bool airborne_latched_{false};
    bool power_off_requested_{false};
    Pending pending_{Pending::None};
    bool dirty_{false};
    bool upload_window_open_{false};
    uint16_t session_{0};
    uint32_t now_ms_{0};
    uint32_t window_opened_ms_{0};
    char pending_buf_[256]{0};
    int pending_len_{0};

    // Long enough to upload ~730 KB over BLE on a slow phone, short enough that
    // a device left on a bench does not stay writable all afternoon.
    static constexpr uint32_t kUploadWindowMs = 10u * 60u * 1000u;
};

}

#endif
