#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PRODUCT_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PRODUCT_H

#include "hardware/boards/lilygo/t_echo_plus/board.h"
#include "products/skyblip_go/services/alarm.h"
#include "products/skyblip_go/services/config.h"
#include "products/skyblip_go/services/ownship.h"
#include "products/skyblip_go/services/power.h"
#include "products/skyblip_go/services/radio.h"
#include "products/skyblip_go/services/screen.h"
#include "products/skyblip_go/services/traffic.h"
#include "runtime/loop.h"

namespace skyblip::go {

enum class Feature : uint32_t {
    AdslRx = 1u << 0,
    UplinkRx = 1u << 1,
    AdslTx = 1u << 6,
    // Receive only: the same M-band dwell as AdslRx, framed by the sync window
    // the two systems share (core/protocol/air.h). We never transmit it.
    AlptasRx = 1u << 7,
    Radar = 1u << 2,
    Alarms = 1u << 3,
    Instruments = 1u << 4,
    CompanionLink = 1u << 5,
};

constexpr Feature kFeatures = static_cast<Feature>(
    static_cast<uint32_t>(Feature::AdslRx) | static_cast<uint32_t>(Feature::UplinkRx) |
    static_cast<uint32_t>(Feature::Radar) | static_cast<uint32_t>(Feature::Alarms) |
    static_cast<uint32_t>(Feature::Instruments) | static_cast<uint32_t>(Feature::CompanionLink) |
    static_cast<uint32_t>(Feature::AdslTx) | static_cast<uint32_t>(Feature::AlptasRx));

// What this product cannot fly without, and what it can lose and keep flying.
constexpr hal::Capabilities kRequired = hal::Capability::Rf | hal::Capability::Gnss;
constexpr hal::Capabilities kOptional =
    hal::Capability::Display | hal::Capability::Baro | hal::Capability::Buzzer |
    hal::Capability::Vibro | hal::Capability::Link | hal::Capability::Storage |
    hal::Capability::Dfu | hal::Capability::Button | hal::Capability::Battery;

// skyBlip Go: one board, one service list. The shell around it only decides how
// often step() is called and where the pixels go.
template <class P>
class Product {
   public:
    using Board = boards::TEchoPlus<P>;

    explicit Product(P& platform) : board_(platform, bus_) {}

    Status setup() {
        const Status s = board_.begin();
        if (s != Status::Ok) return s;
        if (hal::missing(board_.capabilities(), kRequired) != hal::Capability::None)
            return Status::Down;
        const Status loop = loop_.setup();
        state_.started = loop == Status::Ok;
        return loop;
    }

    void step(uint32_t now_ms) {
        board_.poll(state_, now_ms);
        loop_.step(now_ms);
    }

    hal::Capabilities capabilities() const { return board_.capabilities(); }
    hal::Capabilities degraded() const { return hal::missing(board_.capabilities(), kOptional); }

    Board& board() { return board_; }
    bus::Bus& bus() { return bus_; }
    bus::State& state() { return state_; }
    const bus::State& state() const { return state_; }

    OwnshipService& ownship() { return ownship_; }
    PowerService& power() { return power_; }
    RadioService& radio() { return radio_; }
    TrafficService& traffic() { return traffic_; }
    AlarmService& alarm() { return alarm_; }
    ScreenService& screen() { return screen_; }
    ConfigLinkService& config() { return config_; }

   private:
    bus::Bus bus_{};
    bus::State state_{};
    Board board_;
    hal::Roles roles_{board_.roles()};
    runtime::Context ctx_{roles_, bus_, state_};

    ConfigLinkService config_{ctx_};
    OwnshipService ownship_{ctx_};
    PowerService power_{ctx_};
    RadioService radio_{ctx_};
    TrafficService traffic_{ctx_};
    AlarmService alarm_{ctx_};
    ScreenService screen_{ctx_};

    runtime::Service* services_[7]{&config_,  &ownship_, &power_, &radio_,
                                   &traffic_, &alarm_,   &screen_};
    runtime::Loop loop_{services_, 7};
};

}  // namespace skyblip::go

#endif
