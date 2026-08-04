#include "hardware/parts/l76k/l76k.h"

namespace skyblip::parts {

void L76k::send(const char* sentence, uint32_t now_ms) {
    size_t len = 0;
    while (sentence[len]) len++;
    uart_.write(reinterpret_cast<const uint8_t*>(sentence), len);
    last_command_ms_ = now_ms;
}

void L76k::start_sequence(uint32_t now_ms) {
    next_command_ = 0;
    state_ = Config::Sending;
    send_next(now_ms);
}

void L76k::send_next(uint32_t now_ms) {
    send(kCommands[next_command_++], now_ms);
    if (next_command_ < kCommandCount) return;
    state_ = Config::Verifying;
    verify_start_ms_ = now_ms;
    verify_updates_ = parser_.fix().updates;
}

// One byte of nothing, then silence long enough for the receiver to have come
// up. Every attempt starts here, including the ones after a baud change: a
// receiver we could not hear may also have been asleep.
void L76k::begin_wake(uint32_t now_ms) {
    const uint8_t wake = kWakeByte;
    uart_.write(&wake, 1);
    last_command_ms_ = now_ms;
    state_ = Config::Waking;
}

bool L76k::next_baud() {
    if (baud_tried_ >= kBaudCandidateCount) return false;
    const int next = (baud_index_ + 1) % kBaudCandidateCount;
    if (!rate_.set(kBaudCandidates[next])) return false;  // this port cannot retune
    baud_index_ = next;
    baud_tried_++;
    return true;
}

void L76k::verify_failed(uint32_t now_ms) {
    // Two different failures wearing one face. A receiver that answered nothing
    // at all is a receiver we cannot hear, and the rate is the first suspect. A
    // receiver that is talking has the right rate and is simply not obeying, and
    // walking the baud rates would only lose the sentences we do get.
    const bool heard_nothing = parser_.fix().updates == verify_updates_;
    if (heard_nothing && next_baud()) {
        attempts_ = 1;
        begin_wake(now_ms);
        return;
    }
    if (attempts_ >= kMaxConfigAttempts) {
        state_ = Config::Degraded;
        return;
    }
    attempts_++;
    begin_wake(now_ms);
}

void L76k::request_restart(Restart kind) { pending_restart_ = static_cast<uint8_t>(kind); }

void L76k::service(uint32_t now_ms) {
    serviced_ms_ = now_ms;

    if (pending_restart_ != kNoRestart) {
        const bool factory = pending_restart_ == static_cast<uint8_t>(Restart::Factory);
        send(kRestartCommands[pending_restart_], now_ms);
        pending_restart_ = kNoRestart;
        // A factory reset takes the constellations, the sentence set, the
        // dynamic model and the rate with it, so the receiver that comes back is
        // not the one we configured. A cold or warm start only throws away the
        // orbit data, which is the whole point of asking for one.
        if (factory) {
            validity_.reset();
            state_ = Config::Restarting;
        }
        return;
    }

    switch (state_) {
        case Config::Idle:
            attempts_ = 1;
            begin_wake(now_ms);
            break;
        case Config::Restarting:
            if (now_ms - last_command_ms_ < kRestartSettleMs) break;
            attempts_ = 1;
            begin_wake(now_ms);
            break;
        case Config::Waking:
            if (now_ms - last_command_ms_ < kWakeDelayMs) break;
            send(kIdentifyCommand, now_ms);
            state_ = Config::Identifying;
            break;
        case Config::Identifying:
            // Either it named itself or it did not answer in time. It gets the
            // sequence regardless: the only alternative on this board is a
            // receiver left on its factory defaults.
            if (parser_.identified() || now_ms - last_command_ms_ >= kIdentifyWindowMs)
                start_sequence(now_ms);
            break;
        case Config::Sending:
            if (now_ms - last_command_ms_ >= kCommandGapMs) send_next(now_ms);
            break;
        case Config::Verifying:
            if (now_ms - verify_start_ms_ < kVerifyWindowMs) break;
            if (parser_.fix().updates - verify_updates_ >= kMinVerifyUpdates)
                state_ = Config::Ready;
            else
                verify_failed(now_ms);
            break;
        case Config::Ready:
        case Config::Degraded: break;
    }
}

bool L76k::poll(uint32_t now_ms) {
    uint8_t buf[kChunk];
    for (;;) {
        size_t n = uart_.read(buf, sizeof(buf));
        if (n == 0) break;
        for (size_t i = 0; i < n; i++)
            if (parser_.feed(static_cast<char>(buf[i])))
                validity_.observe(parser_.fix(), parser_.last_sentence(), now_ms);
        if (n < sizeof(buf)) break;  // drained
    }

    const bool valid = validity_.check(now_ms) == gnss::FixReject::None;
    const bool fresh = parser_.fix().updates != applied_;
    // A receiver that stops talking publishes nothing, so nothing would ever
    // withdraw the last fix it managed to send. The validity edge is an update in
    // its own right, and it is the one that matters most.
    if (!fresh && valid == fix_.valid) return false;

    applied_ = parser_.fix().updates;
    fix_ = parser_.fix();
    fix_.valid = valid;
    fix_.pps_latency_ms = kPpsLatencyMs;
    return true;
}

}  // namespace skyblip::parts
