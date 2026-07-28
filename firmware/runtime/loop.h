#ifndef SKYBLIP_RUNTIME_LOOP_H
#define SKYBLIP_RUNTIME_LOOP_H

#include "runtime/service.h"

namespace skyblip::runtime {

class Loop {
   public:
    Loop(Service* const* services, int count) : s_(services), n_(count) {}

    Status setup() {
        Status first = Status::Ok;
        for (int i = 0; i < n_; i++) {
            const Status s = s_[i]->setup();
            if (s != Status::Ok && first == Status::Ok) first = s;
        }
        return first;
    }

    void step(uint32_t now_ms) {
        for (int i = 0; i < n_; i++) s_[i]->tick(now_ms);
    }

   private:
    Service* const* s_;
    int n_;
};

}  // namespace skyblip::runtime

#endif
