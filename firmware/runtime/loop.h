#ifndef SKYBLIP_RUNTIME_LOOP_H
#define SKYBLIP_RUNTIME_LOOP_H

#include "runtime/service.h"
#include "runtime/tasks.h"
#include "runtime/watchdog.h"

namespace skyblip::runtime {

class Loop {
   public:
    // names is a parallel array of literals, for the log line that says which
    // service went quiet. Optional: a loop with no names still supervises.
    Loop(Service* const* services, int count, const char* const* names = nullptr)
        : s_(services), n_(count) {
        for (int i = 0; i < n_; i++) feed_.add(names != nullptr ? names[i] : "?", kTaskWatchdogMs);
    }

    Status setup() {
        Status first = Status::Ok;
        for (int i = 0; i < n_; i++) {
            const Status s = s_[i]->setup();
            if (s != Status::Ok && first == Status::Ok) first = s;
        }
        return first;
    }

    void step(uint32_t now_ms) {
        // Registration happens at construction, before there is a time base to
        // stamp, so the deadlines start counting from the first pass.
        if (!feed_.started()) feed_.begin(now_ms);
        for (int i = 0; i < n_; i++) {
            s_[i]->tick(now_ms);
            if (s_[i]->progressing(now_ms)) feed_.check_in(i, now_ms);
        }
    }

    bool may_feed_watchdog(uint32_t now_ms) const { return feed_.may_feed(now_ms); }
    int stalled_service(uint32_t now_ms) const { return feed_.stalled(now_ms); }
    const FeedDecision& feed_decision() const { return feed_; }

   private:
    Service* const* s_;
    int n_;
    FeedDecision feed_{};
};

}  // namespace skyblip::runtime

#endif
