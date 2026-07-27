// devices/models/kvstore.h — a model of the settings store: RAM-backed, a few
// small slots, and it really round-trips. Persistence across a power cycle is
// what it does NOT model, which is exactly the flash behaviour nothing here
// depends on.
#ifndef SKYBLIP_DEVICES_MODELS_KVSTORE_H
#define SKYBLIP_DEVICES_MODELS_KVSTORE_H

#include <string>

#include "hal/kvstore.h"

namespace skyblip::models {

class KvStore : public hal::KvStore {
   public:
    Status read(const char* key, uint8_t* buf, size_t cap, size_t& out_len) override {
        for (auto& e : e_)
            if (e.used && e.key == key) {
                if (e.len > cap) return Status::OutOfRange;
                for (size_t i = 0; i < e.len; i++) buf[i] = e.data[i];
                out_len = e.len;
                return Status::Ok;
            }
        return Status::NotFound;
    }

    Status write(const char* key, const uint8_t* buf, size_t len) override {
        Entry* slot = nullptr;
        for (auto& e : e_)
            if (e.used && e.key == key) slot = &e;
        if (!slot)
            for (auto& e : e_)
                if (!e.used) {
                    slot = &e;
                    slot->key = key;
                    slot->used = true;
                    break;
                }
        if (!slot || len > sizeof(slot->data)) return Status::Full;
        for (size_t i = 0; i < len; i++) slot->data[i] = buf[i];
        slot->len = len;
        return Status::Ok;
    }

    Status erase(const char* key) override {
        for (auto& e : e_)
            if (e.used && e.key == key) e.used = false;
        return Status::Ok;
    }

   private:
    struct Entry {
        bool used{false};
        std::string key;
        uint8_t data[64]{};
        size_t len{0};
    };
    Entry e_[4];
};

}  // namespace skyblip::models

#endif
