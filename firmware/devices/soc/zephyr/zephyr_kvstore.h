// devices/soc/zephyr/zephyr_kvstore.h — hal::KvStore over Zephyr NVS on the
// on-chip flash "storage" partition. Keys are hashed (FNV-1a) to NVS ids.
#ifndef SKYBLIP_DEVICES_SOC_ZEPHYR_ZEPHYR_KVSTORE_H
#define SKYBLIP_DEVICES_SOC_ZEPHYR_ZEPHYR_KVSTORE_H
#if defined(__ZEPHYR__)

#include <zephyr/fs/nvs.h>
#include <zephyr/storage/flash_map.h>

#include "hal/kvstore.h"

namespace skyblip::soc::zephyr {

class ZephyrKvStore : public hal::KvStore {
   public:
    // Mount NVS on the DT-defined `storage_partition`. Returns Ok on success.
    Status begin() {
        fs_.flash_device = FIXED_PARTITION_DEVICE(storage_partition);
        if (!device_is_ready(fs_.flash_device)) return Status::Down;
        fs_.offset = FIXED_PARTITION_OFFSET(storage_partition);
        struct flash_pages_info info;
        if (flash_get_page_info_by_offs(fs_.flash_device, fs_.offset, &info) != 0)
            return Status::Down;
        fs_.sector_size = info.size;
        fs_.sector_count = 3U;
        return nvs_mount(&fs_) == 0 ? Status::Ok : Status::Down;
    }

    Status read(const char* key, uint8_t* buf, size_t cap, size_t& out_len) override {
        ssize_t n = nvs_read(&fs_, id(key), buf, cap);
        if (n <= 0) return Status::NotFound;
        out_len = static_cast<size_t>(n);
        return Status::Ok;
    }
    Status write(const char* key, const uint8_t* buf, size_t len) override {
        ssize_t n = nvs_write(&fs_, id(key), buf, len);
        return (n == static_cast<ssize_t>(len) || n == 0) ? Status::Ok : Status::Full;
    }
    Status erase(const char* key) override {
        return nvs_delete(&fs_, id(key)) == 0 ? Status::Ok : Status::NotFound;
    }

   private:
    static uint16_t id(const char* key) {
        uint32_t h = 2166136261u;
        for (const char* p = key; *p; ++p) h = (h ^ static_cast<uint8_t>(*p)) * 16777619u;
        return static_cast<uint16_t>(h & 0xFFFF) | 1u;  // avoid id 0
    }
    struct nvs_fs fs_{};
};

}  // namespace skyblip::soc::zephyr
#endif  // __ZEPHYR__
#endif
