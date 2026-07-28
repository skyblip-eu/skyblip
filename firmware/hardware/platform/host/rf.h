#ifndef SKYBLIP_HARDWARE_PLATFORM_HOST_RF_H
#define SKYBLIP_HARDWARE_PLATFORM_HOST_RF_H

#include "core/bus/bus.h"
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
        if (plan.mode == hal::RfMode::TxMband && plan.tx == nullptr) return Status::OutOfRange;
        plan_ = plan;
        armed_ = true;
        started_ = false;
        completed_ = false;
        return Status::Ok;
    }

    void abort() override { armed_ = false; }

    void service(uint32_t now_ms) {
        const uint64_t now_us = clock_.micros();
        const uint32_t dt = now_ms - last_ms_;
        last_ms_ = now_ms;
        radio_.service(dt, runtime::kRadioNoRxReinitMs);

        if (armed_ && !started_ && now_us >= plan_.start_us) start();
        if (started_) drain(now_us);
        if (armed_ && now_us >= plan_.end_us) {
            if (!completed_ && plan_.mode == hal::RfMode::TxMband)
                emit(messages::RfEventType::Missed, 0, 0, now_us);
            armed_ = false;
            started_ = false;
        }
    }

    uint32_t armed_count() const { return armed_count_; }

   private:
    void start() {
        started_ = true;
        armed_count_++;
        if (plan_.mode == hal::RfMode::TxMband) {
            radio_.transmit(plan_.tx, plan_.tx_len);
            return;
        }
        if (plan_.freq_hz != 0) {
            parts::MbandConfig cfg{};
            cfg.freq_hz = plan_.freq_hz;
            radio_.configure_mband(cfg);
        }
        radio_.start_receive();
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
    uint32_t last_ms_{0};
    uint32_t armed_count_{0};
    bool armed_{false};
    bool started_{false};
    bool completed_{false};
};

}  // namespace skyblip::platform::host

#endif
