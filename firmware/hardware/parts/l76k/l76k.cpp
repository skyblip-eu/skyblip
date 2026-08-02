#include "hardware/parts/l76k/l76k.h"

namespace skyblip::parts {

void L76k::start_sequence(uint32_t now_ms) {
    next_command_ = 0;
    state_ = Config::Sending;
    send_next(now_ms);
}

void L76k::send_next(uint32_t now_ms) {
    const char* command = kCommands[next_command_++];
    size_t len = 0;
    while (command[len]) len++;
    uart_.write(reinterpret_cast<const uint8_t*>(command), len);
    last_command_ms_ = now_ms;
    if (next_command_ < kCommandCount) return;
    state_ = Config::Verifying;
    verify_start_ms_ = now_ms;
    verify_updates_ = parser_.fix().updates;
}

void L76k::service(uint32_t now_ms) {
    switch (state_) {
        case Config::Idle:
            attempts_ = 1;
            start_sequence(now_ms);
            break;
        case Config::Sending:
            if (now_ms - last_command_ms_ >= kCommandGapMs) send_next(now_ms);
            break;
        case Config::Verifying:
            if (now_ms - verify_start_ms_ < kVerifyWindowMs) break;
            if (parser_.fix().updates - verify_updates_ >= kMinVerifyUpdates)
                state_ = Config::Ready;
            else if (attempts_ >= kMaxConfigAttempts)
                state_ = Config::Degraded;
            else {
                attempts_++;
                start_sequence(now_ms);
            }
            break;
        case Config::Ready:
        case Config::Degraded: break;
    }
}

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
    fix_ = parser_.fix();
    fix_.pps_latency_ms = kPpsLatencyMs;
    return true;
}

}  // namespace skyblip::parts
