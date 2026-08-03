#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_FLASH_REGION_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_FLASH_REGION_H
#if defined(__ZEPHYR__)

#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>

#include "core/flight/log_record.h"
#include "hal/flash_region.h"

namespace skyblip::platform::zephyr {

// The devicetree log_partition: 0x14A000 on the external SPI NOR, on spi1.
//
// Not the radio's bus (the SX1262 has spi3) and not the internal NVMC, whose
// erases and writes halt the CPU while the flash controller owns the bus. A
// program here costs a DMA burst out of SPIM1 and then the part is busy on its
// own time, so a dwell armed against a PPS deadline is never waiting on it.
class FlashRegion : public hal::FlashRegion {
   public:
    Status begin() {
        if (flash_area_open(PARTITION_ID(log_partition), &area_) != 0) return Status::Down;
        const struct device* dev = flash_area_get_device(area_);
        if (dev == nullptr || !device_is_ready(dev)) return Status::Down;
        struct flash_pages_info info;
        // SFDP told the driver the geometry at boot; asking it is how one image
        // serves both candidate parts. The constant is the fallback, not the
        // assertion.
        sector_bytes_ = flash_get_page_info_by_offs(dev, area_->fa_off, &info) == 0
                            ? static_cast<uint32_t>(info.size)
                            : flight::kLogSectorBytes;
        if (sector_bytes_ == 0) return Status::Down;
        sector_count_ = static_cast<uint32_t>(area_->fa_size) / sector_bytes_;
        ready_ = sector_count_ > 0;
        return ready_ ? Status::Ok : Status::Down;
    }

    bool ready() const override { return ready_; }
    uint32_t sector_bytes() const override { return sector_bytes_; }
    uint32_t sector_count() const override { return sector_count_; }

    Status read(uint32_t offset, uint8_t* buf, uint32_t len) override {
        if (!ready_) return Status::Down;
        return flash_area_read(area_, offset, buf, len) == 0 ? Status::Ok : Status::Down;
    }

    Status write(uint32_t offset, const uint8_t* buf, uint32_t len) override {
        if (!ready_) return Status::Down;
        return flash_area_write(area_, offset, buf, len) == 0 ? Status::Ok : Status::Down;
    }

    Status erase_sector(uint32_t index) override {
        if (!ready_) return Status::Down;
        if (index >= sector_count_) return Status::OutOfRange;
        return flash_area_erase(area_, index * sector_bytes_, sector_bytes_) == 0 ? Status::Ok
                                                                                  : Status::Down;
    }

   private:
    const struct flash_area* area_{nullptr};
    uint32_t sector_bytes_{0};
    uint32_t sector_count_{0};
    bool ready_{false};
};

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
