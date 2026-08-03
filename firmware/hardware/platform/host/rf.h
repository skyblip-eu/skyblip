#ifndef SKYBLIP_HARDWARE_PLATFORM_HOST_RF_H
#define SKYBLIP_HARDWARE_PLATFORM_HOST_RF_H

#include "core/bus/bus.h"
#include "core/timing/channel.h"
#include "hal/clock.h"
#include "hal/rf.h"
#include "hardware/parts/sx1262/sx1262.h"
#include "runtime/tasks.h"

namespace skyblip::platform::host {

// The radio executor on virtual time: the same arm/abort contract the silicon
// executor implements, driven by whatever clock the caller advances. Slot
// deadlines are therefore testable to the microsecond with no hardware.
class Rf : public hal::Rf {
   public:
    Rf(parts::Sx1262& radio, hal::Clock& clock, bus::Queue<messages::RfEvent, 8>& out)
        : radio_(radio), clock_(clock), out_(out) {}

    Status begin() override {
        Status s = radio_.begin();
        if (s != Status::Ok) return s;
        return radio_.configure_mband(parts::MbandConfig{});
    }

    Status arm(const hal::RfPlan& plan) override {
        if (plan.end_us <= plan.start_us) return Status::OutOfRange;
        if (plan.tx != nullptr && (plan.tx_at_us < plan.start_us || plan.tx_at_us >= plan.end_us))
            return Status::OutOfRange;
        plan_ = plan;
        armed_ = true;
        started_ = false;
        transmitted_ = false;
        completed_ = false;
        next_carrier_sample_us_ = plan.tx_at_us;
        return Status::Ok;
    }

    void abort() override { armed_ = false; }

    void sleep() override {
        armed_ = false;
        sleeps_++;
        radio_.sleep();
    }

    int sleeps() const { return sleeps_; }

    hal::RfCarrier carrier() const override { return carrier_; }

    void service(uint32_t now_ms) {
        const uint64_t now_us = clock_.micros();
        const uint32_t dt = now_ms - last_ms_;
        last_ms_ = now_ms;
        radio_.service(dt, runtime::kRadioNoRxReinitMs);

        if (armed_ && !started_ && now_us >= plan_.start_us) start();
        if (armed_ && started_ && plan_.tx != nullptr && !transmitted_ && now_us >= plan_.tx_at_us)
            try_transmit(now_us);
        // The receiver keeps reporting between dwells: a frame that arrived
        // while the next plan was being armed is in the chip, not lost.
        if (started_ || radio_.mode() == parts::RadioMode::Rx) drain(now_us);
        if (armed_ && now_us >= plan_.end_us) {
            sample_carrier();
            if (plan_.tx != nullptr && !completed_)
                emit(transmitted_ ? messages::RfEventType::Missed : messages::RfEventType::TxBusy,
                     0, 0, now_us);
            armed_ = false;
            started_ = false;
        }
    }

    uint32_t armed_count() const { return armed_count_; }

   private:
    void start() {
        started_ = true;
        armed_count_++;
        radio_.wake();
        if (plan_.freq_hz != 0) {
            parts::MbandConfig cfg{};
            cfg.freq_hz = plan_.freq_hz;
            cfg.sync = plan_.sync;
            cfg.sync_bits = plan_.sync_bits;
            cfg.payload_bytes = plan_.rx_len;
            radio_.configure_mband(cfg);
        }
        radio_.start_receive();
    }

    void try_transmit(uint64_t now_us) {
        if (plan_.lbt && !carrier_clear(now_us)) return;
        transmitted_ = true;
        radio_.transmit(plan_.tx, plan_.tx_len);
    }

    // The same clear-channel assessment the silicon executor makes, on a clock
    // the caller advances: virtual time does not move inside a service pass, so
    // the EN 300 220-2 V3.3.1 §4.6.3.2 interval is expressed as the run of reads
    // itself and models::Sx1262 answers each one from its level sequence, which
    // is what a 160 us window sees on air.
    int8_t sample_carrier() {
        if (radio_.mode() != parts::RadioMode::Rx) return carrier_.dbm;
        int8_t window[timing::CarrierSense::kSamples];
        for (uint8_t i = 0; i < timing::CarrierSense::kSamples; i++) window[i] = radio_.rssi_inst();
        carrier_.dbm = timing::CarrierSense::mean_dbm(window, timing::CarrierSense::kSamples);
        carrier_.samples++;
        return carrier_.dbm;
    }

    // ADS-L 4 SRD-860 issue 2 §D.3 / §C.2: sample the carrier, and on a busy
    // channel wait a backoff interval before sampling again. Backing off never
    // stops the receiver, and the dwell's end is the only thing that gives up,
    // so a burst is never truncated on air.
    bool carrier_clear(uint64_t now_us) {
        if (now_us < next_carrier_sample_us_) return false;
        if (sample_carrier() < plan_.lbt_threshold_dbm) return true;
        const uint32_t span = plan_.backoff_max_ms - plan_.backoff_min_ms + 1;
        backoff_seed_ = backoff_seed_ * 1664525u + 1013904223u;
        const uint32_t wait_ms = plan_.backoff_min_ms + (backoff_seed_ >> 16) % span;
        next_carrier_sample_us_ = now_us + static_cast<uint64_t>(wait_ms) * 1000;
        return false;
    }

    void drain(uint64_t now_us) {
        uint8_t buf[64];
        for (;;) {
            const parts::RadioEvent ev = radio_.poll(buf, sizeof(buf));
            switch (ev.type) {
                case parts::RadioEventType::None: return;
                case parts::RadioEventType::RxDone:
                    emit(messages::RfEventType::RxDone, buf, ev.len, ev.rssi_dbm, now_us);
                    break;
                case parts::RadioEventType::CrcError:
                    emit(messages::RfEventType::CrcError, 0, 0, now_us);
                    break;
                case parts::RadioEventType::TxDone:
                    completed_ = true;
                    emit(messages::RfEventType::TxDone, 0, 0, now_us);
                    radio_.start_receive();
                    break;
                default: emit(messages::RfEventType::Missed, 0, 0, now_us); return;
            }
        }
    }

    void emit(messages::RfEventType type, const uint8_t* data, uint8_t len, int8_t rssi,
              uint64_t now_us) {
        messages::RfEvent e{};
        e.type = type;
        e.len = len;
        e.rssi_dbm = rssi;
        e.at_us = now_us;
        for (uint8_t i = 0; i < len && i < e.data.size(); i++) e.data[i] = data[i];
        out_.push(e);
    }

    void emit(messages::RfEventType type, uint8_t len, int8_t rssi, uint64_t now_us) {
        emit(type, nullptr, len, rssi, now_us);
    }

    parts::Sx1262& radio_;
    hal::Clock& clock_;
    bus::Queue<messages::RfEvent, 8>& out_;
    hal::RfPlan plan_{};
    hal::RfCarrier carrier_{};
    uint64_t next_carrier_sample_us_{0};
    uint32_t backoff_seed_{0x5eed1262u};
    uint32_t last_ms_{0};
    uint32_t armed_count_{0};
    int sleeps_{0};
    bool armed_{false};
    bool started_{false};
    bool transmitted_{false};
    bool completed_{false};
};

}  // namespace skyblip::platform::host

#endif
