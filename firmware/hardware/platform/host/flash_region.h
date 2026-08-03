#ifndef SKYBLIP_HARDWARE_PLATFORM_HOST_FLASH_REGION_H
#define SKYBLIP_HARDWARE_PLATFORM_HOST_FLASH_REGION_H

#include <cstring>
#include <vector>

#include "hal/flash_region.h"

namespace skyblip::platform::host {

// The log partition as the silicon has it, with NOR's two awkward truths kept
// rather than smoothed away: erased flash reads 0xFF, and a program can only
// clear bits. A fake that lets a caller overwrite a byte would pass tests the
// device fails.
//
// It also knows how to die. cut_power_after(n) programs the next n bytes and
// then refuses everything, which is the one fault that matters here: a record
// half-written when the cell went.
class FlashRegion : public hal::FlashRegion {
   public:
    // 0x14A000 of log_partition in 4 KB sectors, from
    // hardware/boards/lilygo/t_echo_plus/t_echo_plus.dts.
    static constexpr uint32_t kSectorBytes = 4096;
    static constexpr uint32_t kSectorCount = 330;

    explicit FlashRegion(uint32_t sector_count = kSectorCount)
        : sector_count_(sector_count), bytes_(sector_count * kSectorBytes, 0xFF) {}

    bool ready() const override { return present_; }
    void set_present(bool on) { present_ = on; }

    uint32_t sector_bytes() const override { return kSectorBytes; }
    uint32_t sector_count() const override { return sector_count_; }

    Status read(uint32_t offset, uint8_t* buf, uint32_t len) override {
        if (!present_) return Status::Down;
        if (offset + len > bytes_.size()) return Status::OutOfRange;
        std::memcpy(buf, bytes_.data() + offset, len);
        reads++;
        read_bytes += len;
        return Status::Ok;
    }

    Status write(uint32_t offset, const uint8_t* buf, uint32_t len) override {
        if (!present_) return Status::Down;
        if (offset + len > bytes_.size()) return Status::OutOfRange;
        if (dead_) return Status::Down;
        uint32_t n = len;
        if (cut_after_ >= 0) {
            n = static_cast<uint32_t>(cut_after_) < len ? static_cast<uint32_t>(cut_after_) : len;
            cut_after_ = -1;
            dead_ = true;
        }
        for (uint32_t i = 0; i < n; i++) bytes_[offset + i] &= buf[i];
        writes++;
        write_bytes += n;
        return dead_ ? Status::Down : Status::Ok;
    }

    Status erase_sector(uint32_t index) override {
        if (!present_) return Status::Down;
        if (index >= sector_count_) return Status::OutOfRange;
        if (dead_) return Status::Down;
        std::memset(bytes_.data() + index * kSectorBytes, 0xFF, kSectorBytes);
        erases++;
        return Status::Ok;
    }

    // The cell dies partway through the next program, and stays dead until the
    // rig builds a new device - which is what a reboot is.
    void cut_power_after(int32_t bytes) { cut_after_ = bytes; }
    bool dead() const { return dead_; }

    // A second device over the same silicon: what survives a power cut is
    // whatever is in here, so a reboot is a new product handed these bytes.
    const std::vector<uint8_t>& bytes() const { return bytes_; }
    void restore(const std::vector<uint8_t>& image) { bytes_ = image; }

    uint32_t reads{0};
    uint32_t writes{0};
    uint32_t erases{0};
    uint32_t read_bytes{0};
    uint32_t write_bytes{0};

   private:
    uint32_t sector_count_;
    std::vector<uint8_t> bytes_;
    int32_t cut_after_{-1};
    bool dead_{false};
    bool present_{true};
};

}  // namespace skyblip::platform::host

#endif
