// core/comms/frame_budget.h: how many bytes a flat-JSON field costs, and how many
// are left in the frame the central negotiated.
//
// Two reports cut themselves to the link (timing_report.h, diagnostics.h) and
// both refuse rather than truncate, so both need the same arithmetic: a frame
// that is one byte over the negotiated payload is not shortened by the
// controller, it fails. One implementation, so the two cannot drift into
// disagreeing about what fits.
#ifndef SKYBLIP_CORE_COMMS_FRAME_BUDGET_H
#define SKYBLIP_CORE_COMMS_FRAME_BUDGET_H

namespace skyblip::comms::frame {

inline int text_bytes(const char* s) {
    int n = 0;
    while (s[n] != 0) n++;
    return n;
}

// INFO: fc 06aug26 A device counter, as a value a report can carry. json::Writer
// takes long, which is 64-bit on this host and 32-bit on the nRF52, so a naked
// cast of a uint32_t past 2^31 prints a NEGATIVE count on the device and a
// correct one in the test suite - the worst kind of divergence, since the suite is
// where we look. Saturating instead makes both platforms print the same
// characters, and the ceiling is unreachable: at the 5 Hz fix rate this device
// runs, 2147483647 of anything is thirteen years of continuous events. Both
// reports use this one, for the same reason they share the sizing below.
inline long counter(uint32_t v) {
    constexpr uint32_t kCeiling = 0x7FFFFFFFu;
    return static_cast<long>(v > kCeiling ? kCeiling : v);
}

inline int number_bytes(long v) {
    int n = v < 0 ? 1 : 0;
    unsigned long magnitude =
        v < 0 ? static_cast<unsigned long>(-v) : static_cast<unsigned long>(v);
    do {
        n++;
        magnitude /= 10;
    } while (magnitude != 0);
    return n;
}

// "key": - the quotes and the colon, excluding the comma that joins this field
// to the one before it.
inline int key_bytes(const char* key) { return text_bytes(key) + 3; }

inline int int_field_bytes(const char* key, long value) {
    return key_bytes(key) + number_bytes(value);
}

inline int text_field_bytes(const char* key, const char* value) {
    return key_bytes(key) + text_bytes(value) + 2;
}

// "false", the longer of the two, so a field that ends up carrying true has a
// byte to spare rather than a byte too few.
inline int bool_field_bytes(const char* key) { return key_bytes(key) + 5; }

// "more":false, same reasoning.
constexpr int kMoreFieldBytes = 6 + 1 + 5;

// INFO: fc 05aug26 What is left of a frame, counted the way json::Writer spends
// it: the two braces up front, then each field and the comma that joins it to
// the one before. This is why a frame follows the MTU the central negotiated
// instead of the size of a local buffer.
class Budget {
   public:
    explicit Budget(int payload) : left_(payload - 2) {}

    bool take(int bytes) {
        const int need = bytes + (first_ ? 0 : 1);
        if (need > left_) return false;
        left_ -= need;
        first_ = false;
        return true;
    }

   private:
    int left_;
    bool first_{true};
};

}  // namespace skyblip::comms::frame

#endif
