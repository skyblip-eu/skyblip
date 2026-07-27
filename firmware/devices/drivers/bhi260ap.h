// devices/drivers/bhi260ap.h — Bosch BHI260AP smart IMU hub, over io::I2c.
//
// READ THIS BEFORE USING IT: the BHI260AP has NO usable register interface to its
// own IMU. The 6-axis die hangs off the hub's internal master bus, and the host
// interface exposes only virtual sensors served by firmware running on the hub's
// ARC core. Bootloader mode answers exactly seven commands (datasheet Table 31),
// none of which reads a sensor. On the T-Echo Plus the module's HOSTBOOT pad is
// tied high and its QSPI pads are unpopulated, so there is no self-boot from a
// local flash either.
//
// Therefore: no firmware image uploaded => no data. Ever. Not degraded data,
// none. That image is ~101 KB (Bosch BHI260AP.fw), which is a fifth of our
// internal flash budget, so it is NOT linked in. It arrives through
// FirmwareSource, whose only implementation should read the external 2 MB QSPI
// part. Until something provisions that, probe() is the whole useful surface —
// and probe() is genuinely useful, because the IMU is a PLUGGABLE module: a unit
// may carry a BHI260AP, an ICM-20948, or nothing at all.
#ifndef SKYBLIP_DEVICES_DRIVERS_BHI260AP_H
#define SKYBLIP_DEVICES_DRIVERS_BHI260AP_H

#include <cstddef>
#include <cstdint>

#include "core/util/result.h"
#include "devices/io/io.h"

namespace skyblip::drivers {

namespace bhi {

// Host interface registers (BHI260AP datasheet §Host Interface, Table 30).
constexpr uint8_t kRegCommandInput = 0x00;    // channel 0: commands
constexpr uint8_t kRegFifoNonWake = 0x02;     // channel 2: non-wake FIFO
constexpr uint8_t kRegChipCtrl = 0x05;        // CPU turbo
constexpr uint8_t kRegHostIrqCtrl = 0x07;     // HIRQ config (NC on this board)
constexpr uint8_t kRegResetRequest = 0x14;    // host reset
constexpr uint8_t kRegProductId = 0x1C;       // expect kProductId
constexpr uint8_t kRegBootStatus = 0x25;      // bring-up progress bits
constexpr uint8_t kRegChipId = 0x2B;          // expect kChipId
constexpr uint8_t kRegErrorValue = 0x2E;      // non-zero after a failed boot
constexpr uint8_t kRegFifoNonWakeLen = 0x0D;  // bytes waiting in the non-wake FIFO

// Identity (datasheet §5.1). Both are checked: a wrong-but-present device on the
// same address is a fitted ICM-20948 module, not a broken BHI260AP.
constexpr uint8_t kProductId = 0x89;
constexpr uint8_t kChipId = 0x70;

// Boot status bits (datasheet Table 33).
constexpr uint8_t kBootHostInterfaceReady = 1u << 4;
constexpr uint8_t kBootFwVerifyDone = 1u << 5;
constexpr uint8_t kBootFwVerifyError = 1u << 6;

// Bootloader commands (datasheet Table 31 — the only ones valid pre-firmware).
constexpr uint16_t kCmdUploadToProgramRam = 0x0002;
constexpr uint16_t kCmdBootProgramRam = 0x0003;
// Framework commands (valid only once firmware runs).
constexpr uint16_t kCmdConfigureSensor = 0x000D;

// Virtual sensor ids for unfused data (datasheet virtual sensor table).
constexpr uint8_t kSensorAccelPassthrough = 1;
constexpr uint8_t kSensorGyroPassthrough = 10;

}  // namespace bhi

// What the chip turned out to be. Distinguishing Absent from Mismatch matters:
// one means "this unit has no IMU module", the other means "it has a different
// one", and only the second is worth a second probe.
enum class ImuPresence : uint8_t { Absent, Mismatch, Present };

// Where the ~101 KB firmware image comes from. Deliberately a pull interface: it
// lets the image live in external QSPI flash and be streamed in chunks, so it
// never occupies internal flash and never needs a RAM copy.
class FirmwareSource {
   public:
    virtual ~FirmwareSource() = default;
    // Total image size in bytes. MUST be a multiple of 4: the upload command
    // carries its length in 32-bit words.
    virtual size_t size() const = 0;
    // Copy `len` bytes from `offset`. False on any read failure.
    virtual bool read(size_t offset, uint8_t* out, size_t len) = 0;
};

struct ImuSample {
    int16_t x{0}, y{0}, z{0};
};

class Bhi260ap {
   public:
    Bhi260ap(io::I2c& bus, uint8_t addr) : bus_(bus), addr_(addr) {}

    // Identity only. Safe on a bus where nothing is fitted, and the one call
    // worth making before any firmware exists.
    ImuPresence probe();

    // Host-boot the hub: reset, stream the image into program RAM, verify, run.
    // Takes seconds on this board (~104 KB over a 400 kHz I2C bus is ~2.5 s of
    // pure bus time), so it must never run inside a boot-critical path.
    Status boot(FirmwareSource& fw);

    // Ask the running firmware for a virtual sensor at `rate_hz`. Framework
    // command: fails unless boot() succeeded.
    Status configure_sensor(uint8_t sensor_id, float rate_hz, uint32_t latency_ms = 0);

    // Drain the non-wake FIFO once and keep the newest accel/gyro sample.
    // Returns true if a new accel sample arrived.
    bool poll();

    const ImuSample& accel() const { return accel_; }
    const ImuSample& gyro() const { return gyro_; }
    bool booted() const { return booted_; }

   private:
    static constexpr size_t kUploadChunk = 64;  // 4-byte aligned, fits a TWIM burst
    static constexpr size_t kFifoChunk = 64;
    static constexpr int kBootPollTries = 200;

    bool read_reg(uint8_t reg, uint8_t* out, size_t len);
    bool write_reg(uint8_t reg, const uint8_t* data, size_t len);
    bool write_reg8(uint8_t reg, uint8_t value);
    bool wait_boot_bit(uint8_t mask, uint8_t& status);
    void parse_fifo(const uint8_t* data, size_t len, bool& got_accel);

    io::I2c& bus_;
    uint8_t addr_;
    bool booted_{false};
    ImuSample accel_{};
    ImuSample gyro_{};
    uint8_t tx_[4 + kUploadChunk]{};
};

}  // namespace skyblip::drivers

#endif
