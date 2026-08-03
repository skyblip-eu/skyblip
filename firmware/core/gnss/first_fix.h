// core/gnss/first_fix.h: the moment the receiver first solves, and the quiet
// period that follows it. Two separate questions live here: what the pilot is
// told (once, on the first fix ever) and when own-ship state is steady enough to
// be worth transmitting.
#ifndef SKYBLIP_CORE_GNSS_FIRST_FIX_H
#define SKYBLIP_CORE_GNSS_FIRST_FIX_H

#include <cstdint>

namespace skyblip::gnss {

// INFO: hk 02aug26 the moshe-braner SoftRF fork holds transmission for 20 s
// after the first fix and 5 s after a re-fix (SoftRF.ino:552-580). A cold
// receiver's first solutions walk: position, altitude and above all ground
// speed settle over the seconds that follow, and the flight state we derive
// from speed decides our transmit rate. Transmitting through that window
// publishes a track that nobody flew.
constexpr uint32_t kFirstFixSettleMs = 20000;
constexpr uint32_t kRefixSettleMs = 5000;

class FirstFix {
   public:
    void update(bool fix_valid, uint32_t now_ms);

    // True once, on the first valid fix since boot. The caller that annunciates
    // consumes it, so nothing can chirp twice.
    bool take_acquired();

    bool ever_fixed() const { return ever_fixed_; }
    bool has_fix() const { return has_fix_; }
    uint32_t fix_since_ms() const { return fix_since_ms_; }

    // What a transmit policy asks: this device holds a fix, and it has held it
    // long enough for the solution behind it to have settled.
    bool settled(uint32_t now_ms) const;

   private:
    uint32_t fix_since_ms_{0};
    uint32_t settle_ms_{kFirstFixSettleMs};
    bool has_fix_{false};
    bool ever_fixed_{false};
    bool acquired_pending_{false};
};

}  // namespace skyblip::gnss

#endif
