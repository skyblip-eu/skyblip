// core/comms/config.h — the companion-link config state machine (§6, §8 rule 1).
#ifndef SKYBLIP_CORE_COMMS_CONFIG_H
#define SKYBLIP_CORE_COMMS_CONFIG_H

#include "core/messages/messages.h"
#include "core/settings/settings.h"
#include "hal/dfu.h"
#include "hal/link.h"

namespace skyblip::comms {

enum class FlightState : uint8_t { Ground, Airborne, Unknown };

enum class Pending : uint8_t { None, Set, Dfu };

class ConfigService {
   public:
    ConfigService(hal::Link& link, settings::Settings& s, hal::Dfu* dfu = nullptr)
        : link_(link), settings_(s), dfu_(dfu) {}

    void set_flight_state(FlightState fs);
    FlightState flight_state() const { return flight_; }

    void on_link_up(const messages::LinkUp& up);
    void on_link_down(const messages::LinkDown& down);
    void on_rx(const messages::RxFrame& frame);

    void confirm();
    void cancel();

    Pending pending() const { return pending_; }
    bool settings_dirty() const { return dirty_; }
    void clear_dirty() { dirty_ = false; }

    const char* pending_json() const { return pending_buf_; }

   private:
    void reply(const char* json);
    void ack(bool ok, const char* reason);
    bool on_ground() const { return flight_ == FlightState::Ground; }

    hal::Link& link_;
    settings::Settings& settings_;
    hal::Dfu* dfu_;
    FlightState flight_{FlightState::Unknown};
    bool airborne_latched_{false};
    Pending pending_{Pending::None};
    bool dirty_{false};
    uint16_t session_{0};
    char pending_buf_[256]{0};
    int pending_len_{0};
};

}

#endif
