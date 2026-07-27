// BHI260AP driver tests against models/bhi260ap.h.
//
// Scope, stated up front: these pin the BOOT SEQUENCE and the FIFO framing, which
// are our logic. They cannot vouch for register numbers or bit positions — the
// model reads those from the same datasheet as the driver, so an error there is
// common-mode and only silicon settles it (3-ARCHITECTURE §8).
#include <vector>

#include "devices/drivers/bhi260ap.h"
#include "devices/models/bhi260ap.h"
#include "doctest/doctest.h"

using namespace skyblip;

namespace {

// A firmware image that is not 101 KB: the driver streams whatever it is handed,
// so the tests use a small one and the size only matters where it is asserted.
class FakeFirmware : public drivers::FirmwareSource {
   public:
    explicit FakeFirmware(size_t n) : bytes_(n) {
        for (size_t i = 0; i < n; i++) bytes_[i] = static_cast<uint8_t>(i * 7 + 1);
    }
    size_t size() const override { return bytes_.size(); }
    bool read(size_t offset, uint8_t* out, size_t len) override {
        if (fail_at_offset >= 0 && offset >= static_cast<size_t>(fail_at_offset)) return false;
        if (offset + len > bytes_.size()) return false;
        for (size_t i = 0; i < len; i++) out[i] = bytes_[offset + i];
        return true;
    }
    const std::vector<uint8_t>& bytes() const { return bytes_; }
    long fail_at_offset{-1};

   private:
    std::vector<uint8_t> bytes_;
};

}  // namespace

TEST_CASE("bhi260: probe distinguishes absent, wrong part, and present") {
    models::Bhi260ap chip;
    drivers::Bhi260ap imu(chip, models::Bhi260ap::kAddr);

    CHECK(imu.probe() == drivers::ImuPresence::Present);

    // An empty socket: the IMU is a pluggable module, so this is a normal unit.
    chip.on_bus = false;
    CHECK(imu.probe() == drivers::ImuPresence::Absent);

    // Something answering at 0x28 that is not a BHI260AP.
    chip.on_bus = true;
    chip.product_id = 0x12;
    CHECK(imu.probe() == drivers::ImuPresence::Mismatch);
    chip.product_id = drivers::bhi::kProductId;
    chip.chip_id = 0x34;
    CHECK(imu.probe() == drivers::ImuPresence::Mismatch);
}

TEST_CASE("bhi260: boot uploads the whole image and declares its length in words") {
    models::Bhi260ap chip;
    drivers::Bhi260ap imu(chip, models::Bhi260ap::kAddr);
    FakeFirmware fw(2048);

    REQUIRE(imu.boot(fw) == Status::Ok);
    CHECK(imu.booted());
    CHECK(chip.reset_count == 1);
    CHECK(chip.upload_words_declared == 2048 / 4);
    CHECK(chip.uploaded.size() == 2048);
    CHECK(chip.uploaded == fw.bytes());  // byte-exact, in order, nothing dropped
    CHECK(chip.boot_ram_commanded);
    CHECK(chip.running);
}

TEST_CASE("bhi260: a ragged image is refused, because the length is in words") {
    models::Bhi260ap chip;
    drivers::Bhi260ap imu(chip, models::Bhi260ap::kAddr);
    FakeFirmware fw(1023);
    CHECK(imu.boot(fw) == Status::Invalid);
    CHECK_FALSE(imu.booted());
    CHECK(chip.reset_count == 0);  // refused before touching the chip
}

TEST_CASE("bhi260: no image, no boot — and no data") {
    models::Bhi260ap chip;
    drivers::Bhi260ap imu(chip, models::Bhi260ap::kAddr);
    FakeFirmware empty(0);
    CHECK(imu.boot(empty) == Status::Invalid);
    CHECK_FALSE(imu.booted());
    // The whole point of the part: without firmware there is nothing to read.
    chip.queue_sample(drivers::bhi::kSensorAccelPassthrough, 1, 2, 3);
    CHECK_FALSE(imu.poll());
}

TEST_CASE("bhi260: booting an absent module fails without a reset") {
    models::Bhi260ap chip;
    chip.on_bus = false;
    drivers::Bhi260ap imu(chip, models::Bhi260ap::kAddr);
    FakeFirmware fw(256);
    CHECK(imu.boot(fw) == Status::NotFound);
    CHECK(chip.reset_count == 0);
}

TEST_CASE("bhi260: a failed verification is reported, not booted through") {
    models::Bhi260ap chip;
    chip.fail_verify = true;
    drivers::Bhi260ap imu(chip, models::Bhi260ap::kAddr);
    FakeFirmware fw(512);

    CHECK(imu.boot(fw) == Status::Crc);
    CHECK_FALSE(imu.booted());
    CHECK_FALSE(chip.running);
}

TEST_CASE("bhi260: a host interface that never comes up times out") {
    models::Bhi260ap chip;
    chip.never_ready = true;
    drivers::Bhi260ap imu(chip, models::Bhi260ap::kAddr);
    FakeFirmware fw(512);
    CHECK(imu.boot(fw) == Status::Timeout);
    CHECK_FALSE(imu.booted());
}

TEST_CASE("bhi260: a truncated image source aborts the upload") {
    models::Bhi260ap chip;
    drivers::Bhi260ap imu(chip, models::Bhi260ap::kAddr);
    FakeFirmware fw(2048);
    fw.fail_at_offset = 512;  // the external flash stops answering mid-stream

    CHECK(imu.boot(fw) == Status::Invalid);
    CHECK_FALSE(imu.booted());
    CHECK(chip.uploaded.size() < 2048);  // and we did not pretend it completed
}

TEST_CASE("bhi260: framework commands are refused until the firmware runs") {
    models::Bhi260ap chip;
    drivers::Bhi260ap imu(chip, models::Bhi260ap::kAddr);

    // Configure Sensor is a framework command (datasheet Table 31): pre-boot the
    // hub does not merely ignore it, it cannot understand it.
    CHECK(imu.configure_sensor(drivers::bhi::kSensorAccelPassthrough, 50.0f) == Status::Down);
    CHECK(chip.configured_sensors.empty());

    FakeFirmware fw(256);
    REQUIRE(imu.boot(fw) == Status::Ok);
    CHECK(imu.configure_sensor(drivers::bhi::kSensorAccelPassthrough, 50.0f) == Status::Ok);
    CHECK(imu.configure_sensor(drivers::bhi::kSensorGyroPassthrough, 50.0f) == Status::Ok);
    REQUIRE(chip.configured_sensors.size() == 2);
    CHECK(chip.configured_sensors[0] == drivers::bhi::kSensorAccelPassthrough);
    CHECK(chip.configured_sensors[1] == drivers::bhi::kSensorGyroPassthrough);
}

TEST_CASE("bhi260: accel and gyro packets are parsed out of one FIFO drain") {
    models::Bhi260ap chip;
    drivers::Bhi260ap imu(chip, models::Bhi260ap::kAddr);
    FakeFirmware fw(256);
    REQUIRE(imu.boot(fw) == Status::Ok);

    CHECK_FALSE(imu.poll());  // empty FIFO is not a sample

    chip.queue_sample(drivers::bhi::kSensorAccelPassthrough, 100, -200, 4096);
    chip.queue_sample(drivers::bhi::kSensorGyroPassthrough, -1, 2, -3);
    REQUIRE(imu.poll());
    CHECK(imu.accel().x == 100);
    CHECK(imu.accel().y == -200);  // sign preserved through the little-endian pair
    CHECK(imu.accel().z == 4096);
    CHECK(imu.gyro().x == -1);
    CHECK(imu.gyro().y == 2);
    CHECK(imu.gyro().z == -3);
}

TEST_CASE("bhi260: a gyro-only drain reports no new accel sample") {
    models::Bhi260ap chip;
    drivers::Bhi260ap imu(chip, models::Bhi260ap::kAddr);
    FakeFirmware fw(256);
    REQUIRE(imu.boot(fw) == Status::Ok);

    chip.queue_sample(drivers::bhi::kSensorGyroPassthrough, 7, 8, 9);
    CHECK_FALSE(imu.poll());   // poll() reports ACCEL arrival
    CHECK(imu.gyro().x == 7);  // but the gyro was still taken
}

TEST_CASE("bhi260: timestamp and meta-event packets are skipped, not misread") {
    models::Bhi260ap chip;
    drivers::Bhi260ap imu(chip, models::Bhi260ap::kAddr);
    FakeFirmware fw(256);
    REQUIRE(imu.boot(fw) == Status::Ok);

    // A realistic stream: full timestamp, then a meta event, then the sample.
    const uint8_t ts[4] = {0xFC, 0x11, 0x22, 0x33};
    const uint8_t meta[4] = {0xFD, 0x01, 0x00, 0x00};
    chip.queue_raw(ts, sizeof(ts));
    chip.queue_raw(meta, sizeof(meta));
    chip.queue_sample(drivers::bhi::kSensorAccelPassthrough, 11, 22, 33);

    REQUIRE(imu.poll());
    CHECK(imu.accel().x == 11);
    CHECK(imu.accel().y == 22);
    CHECK(imu.accel().z == 33);
}
