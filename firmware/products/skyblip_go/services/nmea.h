// products/skyblip_go/services/nmea.h: the traffic picture and ownship position
// a paired tablet draws, in the sentences every EFB already speaks, and the
// single writer of messages::Endpoint::Nmea.
//
// core/protocol/nmea_out.h has formatted $PFLAA/$PFLAU/$PGRMZ since the first
// week of the project and nothing in the product ever called it: the
// characteristic existed, the endpoint was routed, and a pilot who paired a
// tablet with a skyBlip Go saw an empty map. This is the producer. It owns the
// cadence, the rotation and the framing; it owns no geometry - relative position
// comes from core/protocol, the level from what core/traffic already decided.
#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_NMEA_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_NMEA_H

#include "core/comms/config.h"
#include "core/traffic/alarm.h"
#include "core/traffic/table.h"
#include "products/skyblip_go/features.h"
#include "runtime/service.h"

namespace skyblip::go {

class NmeaService : public runtime::Service {
   public:
    // INFO: nm 04aug26 The cadence is what a moving map needs to draw a target
    // as flying rather than teleporting, and it is not a radio figure. An EFB
    // holds the last position it was given and redraws the symbol there, so the
    // symbol's step is speed times this period: at 1 Hz a 50 m/s aircraft moves
    // 50 m between draws, which reads as motion. Halving it buys nothing - the
    // transmitters on the other side of this are ADS-L and ALP-TAS, both of
    // which key once a second, so a second sentence inside the same second
    // repeats a position that has not changed and spends a notification saying
    // it. Doubling it makes a 100 m jump on the map and delays by a second any
    // alarm the tablet raises for itself. Both SoftRF forks export at exactly
    // this rate (SoftRF.ino isTimeToExport(), 1000 ms, in both trees).
    static constexpr uint32_t kMovingTargetRedrawMs = 1000;

    // INFO: nm 04aug26 What the rotation below guarantees: every target in the
    // table reaches the tablet inside this. It is core/traffic's own freshness
    // rule (kAlertMaxAgeMs), because a contact the device would no longer alarm
    // on is exactly a contact whose position on the tablet has gone stale - the
    // two pictures must not be allowed to disagree by more than the device's own
    // patience with a target.
    static constexpr uint32_t kTargetRefreshBoundMs = traffic::kAlertMaxAgeMs;
    static constexpr int kPassesPerRefreshBound =
        static_cast<int>(kTargetRefreshBoundMs / kMovingTargetRedrawMs);

    // A full table divided over those passes, rounded up: the cap on $PFLAA per
    // pass that makes the bound arithmetic rather than hope.
    static constexpr int kTargetsPerPass =
        (traffic::TrafficTable::kCapacity + kPassesPerRefreshBound - 1) / kPassesPerRefreshBound;
    static_assert(kTargetsPerPass * kPassesPerRefreshBound >= traffic::TrafficTable::kCapacity,
                  "a full table would not be refreshed inside its bound");

    // INFO: nm 04aug26 A pass is formatting and a handful of notifications, none
    // of which stalls the core the way a flash write does
    // (core/timing/durable_write.h), but own-ship keying the transmitter is the
    // one window in the second that owes the radio something. A pass due while a
    // burst is armed waits, and never longer than this, so the cadence above
    // stays a cadence.
    static constexpr uint32_t kPassDeferralCeilingMs = 100;

    // INFO: nm 04aug26 One BLE frame at its largest: 251 bytes of link-layer
    // payload, less four for L2CAP and three for the notification header. A
    // central that negotiates more than this is still served in frames this
    // size, because no controller puts more than this in one air packet anyway.
    static constexpr int kFrameBytesCap = 244;

    // Wider than the worst case test/core/test_nmea_out.cpp pins for these three
    // sentences, and checked at the point of use rather than assumed.
    static constexpr int kSentenceBytesCap = 128;

    NmeaService(runtime::Context& context, Feature declared)
        : runtime::Service(context), enabled_(has_feature(declared, Feature::CompanionLink)) {}

    void tick(uint32_t now_ms) override;

    // Who knows whether a central is there: comms::ConfigService is the single
    // reader of bus.link_events, so this service asks it rather than draining a
    // queue that would then be short an event for whoever drains it second.
    void attach_config(const comms::ConfigService& config) { config_ = &config; }

    // Frames the link would not take. Silence towards a phone is a fault worth a
    // number, the way core/comms counts its own (ConfigService::link_drops).
    uint32_t link_drops() const { return link_drops_; }
    bool enabled() const { return enabled_; }

   private:
    bool listening() const;
    void run_pass();
    void emit_status();
    void emit_ownship();
    void emit_altitude();
    void emit_targets();
    void write(const char* bytes, int len);
    void flush();

    const comms::ConfigService* config_{nullptr};
    char frame_[kFrameBytesCap]{};
    char sentence_[kSentenceBytesCap]{};
    uint32_t last_pass_ms_{0};
    uint32_t link_drops_{0};
    int payload_{hal::kMinimumLinkPayload};
    int frame_len_{0};
    // Where the rotation resumes. It advances only past a target that actually
    // went out, so a pass cut short by a refusing link resends its tail first.
    int cursor_{0};
    bool stalled_{false};
    bool passed_once_{false};
    const bool enabled_;
};

}  // namespace skyblip::go

#endif
