// core/timing/durable_write.h: whether a durable write may start now, and how
// long one may wait. The single owner of that question.
//
// 1-ARCHITECTURE.md §5.1's "no flash work inside a dwell" is a statement about
// the nRF52840's INTERNAL flash: the NVMC owns the memory while it writes or
// erases, so the stall reaches every thread and every interrupt whose code lives
// there - the PPS latch (hardware/platform/zephyr/pps.h timestamps the edge
// inside the GPIO callback) and the SX1262's DIO1 among them - on the core that
// arms PPS-anchored deadlines. The dwell map (core/timing/slot.h) leaves no
// unarmed phase in the second: 0..200 is slot 1's tail, 205..395 the uplink
// dwell, 400..1200 the two M-band dwells, and the only gaps are the two 5 ms
// retune guards, which are the last place a stall belongs. So this does not look
// for a phase with no dwell in it. It places the whole stall where the CPU owes
// the radio nothing: inside an armed RECEIVE dwell, finishing a guard's width
// before that dwell has to be re-armed, clear of the window own-ship may key the
// PA in, and clear of the top of the second the anchor is latched on.
//
// What that costs is the receiver, and it is worth saying out loud: the dwell
// stays armed but the core is deaf to its reports for the length of the write, so
// one settings write is up to kWorstWriteMs of one channel not being heard. That
// is why the write is a request that coalesces rather than a call, and why the
// bound below is measured from the first change and not the last.
#ifndef SKYBLIP_CORE_TIMING_DURABLE_WRITE_H
#define SKYBLIP_CORE_TIMING_DURABLE_WRITE_H

#include <cstdint>

#include "core/timing/slot.h"

namespace skyblip::timing {

// What only the service that arms dwells knows: where in the second it believes
// it is, and whether the dwell it armed carries a burst whose outcome has not
// come back yet. Published once per pass, so this policy reads the radio's own
// view of the second rather than keeping a second copy of the slot arithmetic.
// Stamped, because a view sampled when the radio ran is already stale by the
// time the service that asks gets its turn.
struct DwellPhase {
    uint32_t at_ms{0};
    int phase_ms{0};
    bool armed{false};
    bool burst_armed{false};
};

// Idle: nothing to write. Hold: something to write, not now. Place: now, inside
// the window. Forced: the bound is spent and it goes anyway - the one outcome
// that is a fault and is counted as one.
enum class DurableWriteVerdict : uint8_t { Idle, Hold, Place, Forced };

class DurableWriteWindow {
   public:
    // INFO: fw 04aug26 nRF52840 PS v1.8, NVMC chapter, "Electrical
    // specification": tWRITE = 41 us to write one 32-bit word, tERASEPAGE =
    // 85 ms to erase one 4 kB page. Flash cannot be read while the NVMC is
    // writing or erasing it, which is why these are CPU stalls and not background
    // work - the core fetches its own instructions from the memory being erased.
    static constexpr uint32_t kWordWriteUs = 41;
    static constexpr uint32_t kPageEraseMs = 85;

    // INFO: fw 04aug26 What one settings write costs in the worst case. NVS
    // (hardware/platform/zephyr/kvstore.h, three sectors of the internal
    // storage_partition) appends until a sector is full, then closes it and
    // garbage-collects the next one: one tERASEPAGE, plus the live entries
    // copied forward a word at a time. The settings blob is 64 bytes, so the
    // copy is kBlobWords words. Budgeted from the datasheet, not measured - the
    // bench figure is what settles it.
    static constexpr uint32_t kBlobWords = 16;
    static constexpr uint32_t kWorstWriteMs =
        kPageEraseMs + (kBlobWords * kWordWriteUs + 999) / 1000;

    // INFO: fw 04aug26 The longest SINGLE stall inside that worst case, which is
    // a different number: CONFIG_SOC_FLASH_NRF_PARTIAL_ERASE splits a page erase
    // into ERASEPAGEPARTIAL slices of CONFIG_SOC_FLASH_NRF_PARTIAL_ERASE_MS and
    // the flash is readable between them (both set in
    // products/skyblip_go/prj.conf). kWorstWriteMs of wall clock still passes;
    // what shrinks is how long the core is held at once, and the static_assert
    // below is what that buys - a write that had to go outside its window cannot
    // move a dwell edge further than kJitterGuardMs, the guard both band edges
    // keep, already absorbs. It is NOT under kHopGuardMs, and cannot be: the
    // Kconfig floor for a slice is 2 ms and the M-band hop guard is 1. That is
    // one more reason a forced write is counted as the fault it is.
    static constexpr uint32_t kPartialEraseMs = 4;

    // How long the policy waits for a stream of changes to stop before it writes
    // the one blob they add up to. The panel steps a value on every tap that
    // lands inside ui::ConfirmGesture::kDoublePressMs (600 ms), so a settle
    // shorter than that window would put a pilot's six taps on flash six times.
    // This is that window plus a service pass and a little.
    static constexpr uint32_t kSettleMs = 750;

    // The hard bound, measured from the FIRST unwritten change rather than the
    // last: a setting a pilot changed that is still not on flash when the cell
    // dies is a setting they will believe they changed. Three seconds, the same
    // figure §D.3 already treats as waited-long-enough for a burst that cannot
    // find a clear channel. Long enough that the settle above and the widest gap
    // between two free windows cannot spend it between them; short enough that
    // what a pilot loses to a dead cell is the change they were still making.
    static constexpr uint32_t kMaxDeferMs = 3000;

    // A published view older than this is not evidence about where the second is
    // any more, so it refuses rather than guesses. Ten service passes, and wider
    // than the coarsest step anything drives the loop at: the phase carried
    // forward from a stamped view stays exact as long as the clock does, and a
    // view that outran its own dwell is refused by the window test anyway. This is
    // for the other case - a radio that stopped publishing at all.
    static constexpr uint32_t kViewStaleMs = 100;

    // The two stretches the second actually offers, proved against the map they
    // are cut from rather than restated: slot 1's tail, and the uplink dwell.
    static_assert(kWorstWriteMs + static_cast<uint32_t>(kJitterGuardMs) <
                      static_cast<uint32_t>(kSlot1Wrap),
                  "one settings write no longer fits inside slot 1's tail");
    static_assert(kWorstWriteMs + static_cast<uint32_t>(kJitterGuardMs) <
                      static_cast<uint32_t>(kUplinkRxEnd - kUplinkRxStart),
                  "one settings write no longer fits inside the uplink dwell");
    static_assert(kPartialEraseMs < static_cast<uint32_t>(kJitterGuardMs),
                  "a forced write could move a dwell edge past the slot map's guard");
    static_assert(kMaxDeferMs > kSettleMs + 1000,
                  "the settle and one whole second could spend the bound between them");

    // A change was accepted. Coalescing lives here: the first call starts the
    // bound, every call restarts the settle, and the blob that eventually goes
    // to flash is whichever one the caller holds by then.
    void request(uint32_t now_ms);

    // Pure: the same arguments give the same answer, and nothing here reads a
    // clock of its own.
    DurableWriteVerdict decide(const SlotPlan& plan, const DwellPhase& dwell,
                               uint32_t now_ms) const;

    // The write happened. `forced` is the verdict that placed it, not a guess.
    void placed(uint32_t now_ms, bool forced);

    bool pending() const { return pending_; }

    // What the bench reads: how many changes arrived, how many writes they cost
    // (the coalescing ratio), how many could not be placed inside the bound, and
    // the longest a change ever waited.
    uint32_t requests() const { return requests_; }
    uint32_t writes() const { return writes_; }
    uint32_t forced() const { return forced_; }
    uint32_t worst_wait_ms() const { return worst_wait_ms_; }

    // Exposed so the window itself is testable at a phase, without a request and
    // a clock in front of it.
    static bool free_at(const SlotPlan& plan, int phase_ms, uint32_t cost_ms);

   private:
    bool placeable(const SlotPlan& plan, const DwellPhase& dwell, uint32_t now_ms) const;

    uint32_t requests_{0};
    uint32_t writes_{0};
    uint32_t forced_{0};
    uint32_t worst_wait_ms_{0};
    uint32_t first_request_ms_{0};
    uint32_t last_request_ms_{0};
    bool pending_{false};
};

}  // namespace skyblip::timing

#endif
