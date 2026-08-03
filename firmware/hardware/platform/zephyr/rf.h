#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_RF_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_RF_H
#if defined(__ZEPHYR__)

#include <zephyr/kernel.h>

#include "core/bus/bus.h"
#include "core/timing/channel.h"
#include "hal/clock.h"
#include "hal/rf.h"
#include "hardware/parts/sx1262/sx1262.h"
#include "runtime/tasks.h"

namespace skyblip::platform::zephyr {

// The radio executor on silicon: its own thread, above every other application
// thread, waking on an armed absolute deadline. Nothing on this thread writes
// flash or touches the BLE stack: a deferred internal-flash write blocks for
// milliseconds, which is more than the whole guard budget.
class Rf : public hal::Rf {
   public:
    static constexpr int kStackSize = 2048;
    static constexpr int kSpinUs = 200;
    // How long the thread waits for the next dwell before it looks at the
    // radio's health instead. Short against the 30 s no-RX rope, long enough
    // that an idle device is not woken for nothing.
    static constexpr int kHealthTickMs = 250;

    Rf(parts::Sx1262& radio, hal::Clock& clock, bus::Queue<messages::RfEvent, 8>& out)
        : radio_(radio), clock_(clock), out_(out) {
        k_sem_init(&armed_, 0, 1);
    }

    Status begin() override {
        Status s = radio_.begin();
        if (s != Status::Ok) return s;
        s = radio_.configure_radio(parts::RadioConfig{});
        if (s != Status::Ok) return s;
        tid_ = k_thread_create(&thread_, stack_, K_THREAD_STACK_SIZEOF(stack_), entry, this,
                               nullptr, nullptr,
                               K_PRIO_COOP(static_cast<int>(runtime::TaskPrio::Rf)), 0, K_NO_WAIT);
        k_thread_name_set(tid_, "rf_exec");
        return Status::Ok;
    }

    Status arm(const hal::RfPlan& plan) override {
        if (plan.end_us <= plan.start_us) return Status::OutOfRange;
        if (plan.tx != nullptr && (plan.tx_at_us < plan.start_us || plan.tx_at_us >= plan.end_us))
            return Status::OutOfRange;
        // A dwell that cannot start before its own end is refused here rather
        // than truncated on air.
        if (clock_.micros() >= plan.end_us) {
            emit(messages::RfEventType::Missed);
            return Status::WouldBlock;
        }
        plan_ = plan;
        k_sem_give(&armed_);
        return Status::Ok;
    }

    void abort() override { abort_ = true; }

    // The shutdown path runs on the service thread and the radio belongs to this
    // one, so what crosses the boundary is a request: abort the dwell, wake the
    // thread, and let it issue SetSleep itself. Nothing else may touch the SPI
    // while a dwell is on it.
    void sleep() override {
        sleep_requested_ = true;
        abort_ = true;
        k_sem_give(&armed_);
    }

    hal::RfCarrier carrier() const override { return carrier_; }

    // The board calls this from the service pass, and there is deliberately
    // nothing here: the radio belongs to the thread below, and reinitialising it
    // from the service list would put a second writer on the SPI bus while a
    // dwell is using it. The health watchdog runs in run(), where the chip is
    // owned.
    void service(uint32_t) {}

   private:
    static void entry(void* self, void*, void*) { static_cast<Rf*>(self)->run(); }

    void run() {
        health_us_ = clock_.micros();
        for (;;) {
            const int armed = k_sem_take(&armed_, K_MSEC(kHealthTickMs));
            health();
            if (armed != 0) continue;
            if (sleep_requested_) {
                sleep_requested_ = false;
                radio_.sleep();
                continue;
            }
            abort_ = false;
            const hal::RfPlan plan = plan_;
            sleep_until(plan.start_us);
            if (abort_) continue;
            start(plan);
            dwell(plan);
            health();
        }
    }

    // A receiver that has heard nothing for 30 s is deaf, not lucky, and the
    // only cure is a reinitialisation. It is accumulated here, between dwells,
    // because this thread owns the bus: the elapsed time comes off the same
    // clock the deadlines do, so a dwell that overran is counted, not lost.
    void health() {
        const uint64_t now_us = clock_.micros();
        if (now_us <= health_us_) return;
        const uint32_t elapsed_ms = static_cast<uint32_t>((now_us - health_us_) / 1000);
        if (elapsed_ms == 0) return;
        health_us_ += static_cast<uint64_t>(elapsed_ms) * 1000;
        radio_.service(elapsed_ms, runtime::kRadioNoRxReinitMs);
    }

    void sleep_until(uint64_t deadline_us) {
        const uint64_t now = clock_.micros();
        if (deadline_us <= now) return;
        const uint64_t left = deadline_us - now;
        if (left > 2000) k_usleep(static_cast<int32_t>(left - 1000));
        while (clock_.micros() < deadline_us) k_busy_wait(10);
    }

    void start(const hal::RfPlan& plan) {
        radio_.wake();
        band_ = plan.mode == hal::RfMode::RxOband ? messages::Band::O : messages::Band::M;
        if (plan.freq_hz != 0) radio_.configure_radio(dwell_config(plan));
        radio_.start_receive();
    }

    // The whole modem, not just the synthesiser: the two bands are two
    // modulations (ADS-L 4 SRD-860 issue 2 §C.2 against §C.4) and the plan
    // carries both halves.
    static parts::RadioConfig dwell_config(const hal::RfPlan& plan) {
        parts::RadioConfig cfg{};
        cfg.freq_hz = plan.freq_hz;
        if (plan.bitrate != 0) cfg.bitrate = plan.bitrate;
        if (plan.fdev_hz != 0) cfg.fdev_hz = plan.fdev_hz;
        if (plan.bandwidth_hz != 0) cfg.bandwidth_hz = plan.bandwidth_hz;
        cfg.gaussian_bt_e2 = plan.gaussian_bt_e2;
        cfg.sync = plan.sync;
        cfg.sync_bits = plan.sync_bits;
        cfg.payload_bytes = plan.rx_len;
        return cfg;
    }

    // §C.2 backoff interval, drawn per failed carrier sample.
    uint64_t backoff_us(const hal::RfPlan& plan) {
        const uint32_t span = plan.backoff_max_ms - plan.backoff_min_ms + 1;
        backoff_seed_ = backoff_seed_ * 1664525u + 1013904223u;
        return static_cast<uint64_t>(plan.backoff_min_ms + (backoff_seed_ >> 16) % span) * 1000;
    }

    // One clear-channel assessment, reported and not judged. The chip has no
    // averaging block, so the interval of EN 300 220-2 V3.3.1 §4.6.3.2 is a run
    // of GetRssiInst reads spaced across it; the last read is at least
    // kAssessmentUs after the first whatever a read costs on this SPI bus. The
    // combination and the threshold in front of it are core/timing/channel.h's.
    int8_t sample_carrier() {
        if (radio_.mode() != parts::RadioMode::Rx) return carrier_.dbm;
        int8_t window[timing::CarrierSense::kSamples];
        const uint64_t opened_us = clock_.micros();
        for (uint8_t i = 0; i < timing::CarrierSense::kSamples; i++) {
            const uint64_t due_us =
                opened_us + static_cast<uint64_t>(i) * timing::CarrierSense::kSampleSpacingUs;
            while (clock_.micros() < due_us) k_busy_wait(1);
            window[i] = radio_.rssi_inst();
        }
        carrier_.dbm = timing::CarrierSense::mean_dbm(window, timing::CarrierSense::kSamples);
        carrier_.samples++;
        return carrier_.dbm;
    }

    void dwell(const hal::RfPlan& plan) {
        bool completed = false;
        bool transmitted = false;
        // §D.3: carrier sense, then a backoff interval before the next sample.
        // Backing off never stops the receiver, and the dwell's end is what
        // gives up, so a burst is never truncated on air.
        uint64_t next_carrier_sample_us = plan.tx_at_us;
        while (!abort_ && clock_.micros() < plan.end_us) {
            const uint64_t now_us = clock_.micros();
            if (plan.tx != nullptr && !transmitted && now_us >= next_carrier_sample_us) {
                if (!plan.lbt || sample_carrier() < plan.lbt_threshold_dbm) {
                    transmitted = true;
                    radio_.transmit(plan.tx, plan.tx_len);
                } else {
                    next_carrier_sample_us = now_us + backoff_us(plan);
                }
            }
            const parts::RadioEvent ev = radio_.poll(rx_.data.data(), messages::kRfEventBytes);
            switch (ev.type) {
                case parts::RadioEventType::None: k_usleep(kSpinUs); continue;
                case parts::RadioEventType::RxDone: push_rx(ev); break;
                case parts::RadioEventType::CrcError: emit(messages::RfEventType::CrcError); break;
                case parts::RadioEventType::TxDone:
                    completed = true;
                    emit(messages::RfEventType::TxDone);
                    radio_.start_receive();
                    break;
                default: emit(messages::RfEventType::Missed); return;
            }
        }
        sample_carrier();
        if (plan.tx != nullptr && !completed)
            emit(transmitted ? messages::RfEventType::Missed : messages::RfEventType::TxBusy);
    }

    // The frame is already in the event that will carry it. An O-band uplink
    // codeword is 255 bytes, and staging one on this thread's 2 KB stack as well
    // as on the queue would be the same bytes twice. The band the dwell was
    // armed for travels with it: the O band carries one system and the M band
    // two, and only the arming knows which of them this burst is.
    void push_rx(const parts::RadioEvent& ev) {
        rx_.type = messages::RfEventType::RxDone;
        rx_.band = band_;
        rx_.len = ev.len;
        rx_.rssi_dbm = ev.rssi_dbm;
        rx_.at_us = clock_.micros();
        out_.push(rx_);
    }

    void emit(messages::RfEventType type, uint8_t len = 0, int8_t rssi = 0) {
        messages::RfEvent e{};
        e.type = type;
        e.band = band_;
        e.len = len;
        e.rssi_dbm = rssi;
        e.at_us = clock_.micros();
        out_.push(e);
    }

    parts::Sx1262& radio_;
    hal::Clock& clock_;
    bus::Queue<messages::RfEvent, 8>& out_;
    hal::RfPlan plan_{};
    hal::RfCarrier carrier_{};
    messages::RfEvent rx_{};
    messages::Band band_{messages::Band::M};
    uint32_t backoff_seed_{0x5eed1262u};
    uint64_t health_us_{0};
    struct k_sem armed_{};
    struct k_thread thread_{};
    k_tid_t tid_{nullptr};
    K_KERNEL_STACK_MEMBER(stack_, kStackSize);
    volatile bool abort_{false};
    volatile bool sleep_requested_{false};
};

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
