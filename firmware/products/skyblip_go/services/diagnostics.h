#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_DIAGNOSTICS_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_DIAGNOSTICS_H

#include "core/bus/state.h"
#include "core/comms/diagnostics.h"

namespace skyblip::go {

// The dump's collector: it reads counters the device already keeps and writes
// them into the one snapshot the companion link answers from
// (core/comms/diagnostics.h), then puts the same table on the console every
// kDiagnosticsDumpMs. No formatting and no policy live here - that is core's, and
// host-tested there.
//
// Not a runtime::Service, deliberately. A service is handed roles, the bus and
// bus::State; six of these numbers belong to sibling services and five to the GNSS
// part, and neither is reachable through a Context. Reaching for them would mean
// publishing counters through the bus for one reader, so instead this is driven
// from the shell with the product in hand, which is also the only place that knows
// how to write to a console.
//
// Sink is whatever can take a line: platform::zephyr::Console on silicon, a
// capturing sink in the tests. It answers ready() so a device with no host on its
// USB port formats nothing at all.
template <class Product, class Sink>
class DiagnosticsDump {
   public:
    void step(Product& product, uint32_t now_ms) {
        comms::Diagnostics& snapshot = product.config().config().diagnostics();
        // Unsigned subtraction throughout, so the 49.7-day wrap of a 32-bit
        // millisecond counter costs one late dump and nothing else.
        if (!started_ || now_ms - refreshed_ms_ >= comms::kDiagnosticsRefreshMs) {
            refresh(product, snapshot, now_ms);
            refreshed_ms_ = now_ms;
        }
        if (!started_ || now_ms - dumped_ms_ >= comms::kDiagnosticsDumpMs) {
            dumped_ms_ = now_ms;
            emit(snapshot);
        }
        started_ = true;
    }

    Sink& sink() { return sink_; }

   private:
    // Every read here is a const accessor of a counter that lives somewhere else.
    // Nothing is cleared by being read: a dump is a witness, and a witness that
    // consumes its evidence makes the second reader wrong.
    void refresh(Product& product, comms::Diagnostics& d, uint32_t now_ms) {
        const bus::State& state = product.state();

        d.uptime_s = now_ms / 1000;

        d.noise_dbm = product.radio().noise_floor().dbm();
        d.lbt_dbm = product.radio().lbt_threshold_dbm();
        d.duty_permille = product.radio().duty_permille(now_ms);
        d.gave_up = product.radio().gave_up_count();
        d.rx_ok = state.rx_ok;
        d.rx_bad = state.rx_bad;
        d.tx_ok = state.tx_ok;
        d.tx_busy = state.tx_busy;

        d.tracked = static_cast<uint32_t>(state.traffic.count());
        d.alarm = state.alarm_level;

        d.gnss_fixes = state.gnss_fixes;
        d.fix_valid = state.own.fix_valid;
        d.resid_m = state.own.pred_resid_m;
        d.resid_valid = state.own.pred_resid_valid;

        // The receiver's own answers, which nothing else on the device reads: a
        // unit talking at a rate the devicetree did not ask for, or one whose fixes
        // are being refused for a named reason, is the support case this part
        // generates. The part is reached through the board because it is a part and
        // not a role - there is no hal:: seam that could carry a firmware string.
        const auto& gnss = product.board().gnss();
        d.gnss_baud = gnss.baud_rate();
        d.gnss_identified = gnss.identified();
        d.gnss_firmware = gnss.firmware_version();
        d.gnss_reject = gnss.reject_reason();
        d.gnss_rejected = gnss.rejected();

        // The two counters core/power keeps and acts on nothing for: comparator
        // firings, and readings the sanity floor threw away. They tell a bench a
        // supply problem from a divider problem and they had no reader at all.
        d.supply_warnings = product.power().supply_warnings();
        d.battery_implausible = product.power().implausible_samples();

        d.refreshes++;
    }

    void emit(const comms::Diagnostics& d) {
        if (!sink_.ready()) return;
        comms::DiagnosticsReport report(d, "diag");
        char line[comms::DiagnosticsReport::kLineCap];
        for (int i = 0; i < report.line_count(); i++) {
            const int len = report.line(i, line, static_cast<int>(sizeof(line)));
            if (len > 0) sink_.line(line, len);
        }
    }

    Sink sink_{};
    uint32_t refreshed_ms_{0};
    uint32_t dumped_ms_{0};
    bool started_{false};
};

}  // namespace skyblip::go

#endif
