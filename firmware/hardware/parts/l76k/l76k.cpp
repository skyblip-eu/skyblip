#include "hardware/parts/l76k/l76k.h"

namespace skyblip::parts {

bool L76k::poll() {
    uint8_t buf[kChunk];
    for (;;) {
        size_t n = uart_.read(buf, sizeof(buf));
        if (n == 0) break;
        for (size_t i = 0; i < n; i++) parser_.feed(static_cast<char>(buf[i]));
        if (n < sizeof(buf)) break;  // drained
    }
    if (parser_.fix().updates == applied_) return false;
    applied_ = parser_.fix().updates;
    return true;
}

}  // namespace skyblip::parts
