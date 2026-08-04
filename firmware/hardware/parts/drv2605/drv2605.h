// TI DRV2605 haptic driver, I2C 0x5A, with its enable line on a GPIO.
//
// This part exists because P0.08 on the T-Echo Plus is NOT a motor drive. LilyGO
// and Meshtastic call it PIN_DRV_EN / PIN_MOTOR_EN, and SoftRF identifies the
// Plus by finding a DRV2605 at 0x5A next to the BHI260AP
// (platform/nRF52.cpp:1158-1163, address at platform/nRF52.h:136). Driving that
// pin high brings the driver out of hardware standby and produces no pulse at
// all: the part needs a mode and a drive value over I2C before the motor moves
// (nRF52.cpp:2112-2133 configures it, 3383-3385 plays an effect, 2901 stops it).
//
// Register map and the ERM open-loop bring-up follow SensorLib's driver, which
// is the vendor library SoftRF uses: HapticDriver_DRV2605_Reg.hpp for the
// addresses and HapticDriver_DRV2605.cpp:410-456 for the sequence.
#ifndef SKYBLIP_HARDWARE_PARTS_DRV2605_H
#define SKYBLIP_HARDWARE_PARTS_DRV2605_H

#include "core/util/result.h"
#include "hal/haptic.h"
#include "hardware/io/io.h"

namespace skyblip::parts {

class Drv2605 : public hal::Haptic {
   public:
    // SoftRF platform/nRF52.h:136 (DRV2605_ADDRESS), and the datasheet's only
    // address: the part has no address-select pin.
    static constexpr uint8_t kAddress = 0x5A;

    Drv2605(io::I2c& bus, io::Gpio& gpio, int enable_pin)
        : bus_(bus), gpio_(gpio), enable_pin_(enable_pin) {}

    // The enable line, raised before anything on the bus is believed. SoftRF
    // configures the part over I2C first and raises the pin afterwards, which is
    // evidence that the chip answers with this pin undriven - it found 0x5A at
    // nRF52.cpp:1158 long before nRF52.cpp:2130 drove the pin. We raise it
    // first anyway: it costs one register write, and it is the order that also
    // works if the line turns out to gate the motor's supply rather than the
    // driver's standby.
    void power_up();

    // Identify and configure, or report absent. Never asserts presence from the
    // pin: the address has to answer and the part has to name itself.
    Status begin();

    // hal::Haptic. Real-time playback, not a ROM effect: the drive value is a
    // register that holds until it is cleared, so the pulse is exactly as long
    // as the caller's timer, and no entry in the effect ROM has to be trusted to
    // last a particular number of milliseconds. The ROM route (setWaveform +
    // GO, which is what SoftRF plays) gives the effect's own length, which is
    // not the length core/annunciation asked for.
    void start() override;
    void stop() override;

    // Standby and the enable line released, for the way down.
    void park();

    bool ready() const { return ready_; }
    // STATUS[7:5]. 3 DRV2605, 4 DRV2604, 5 DRV2605X, 6 DRV2604L, 7 DRV2605L.
    uint8_t device_id() const { return device_id_; }

   private:
    static constexpr uint8_t kRegStatus = 0x00;
    static constexpr uint8_t kRegMode = 0x01;
    static constexpr uint8_t kRegRtpInput = 0x02;
    static constexpr uint8_t kRegLibrary = 0x03;
    static constexpr uint8_t kRegWaveSeq1 = 0x04;
    static constexpr uint8_t kRegWaveSeq2 = 0x05;
    static constexpr uint8_t kRegGo = 0x0C;
    static constexpr uint8_t kRegOverdrive = 0x0D;
    static constexpr uint8_t kRegSustainPositive = 0x0E;
    static constexpr uint8_t kRegSustainNegative = 0x0F;
    static constexpr uint8_t kRegBreak = 0x10;
    static constexpr uint8_t kRegAudioMax = 0x13;
    static constexpr uint8_t kRegFeedback = 0x1A;
    static constexpr uint8_t kRegControl3 = 0x1D;

    static constexpr uint8_t kModeInternalTrigger = 0x00;
    static constexpr uint8_t kModeRealTimePlayback = 0x05;
    // MODE bit 6. Software standby: the bus stays alive, the driver stage does
    // not, which is where the part belongs between pulses.
    static constexpr uint8_t kModeStandby = 0x40;
    // FEEDBACK bit 7 clear selects ERM rather than an LRA; CONTROL3 bit 5 is
    // ERM_OPEN_LOOP. Open loop because the fitted motor is not in any datasheet
    // we have: closed loop needs a rated voltage and a back-EMF the part can
    // trust, and an unknown motor gives neither.
    static constexpr uint8_t kFeedbackLraSelect = 0x80;
    static constexpr uint8_t kControl3ErmOpenLoop = 0x20;
    // ERM library A. Not used by real-time playback, but a library has to be
    // selected for the part to be in a defined state, and 1 is what both
    // SoftRF (nRF52.cpp:2121) and SensorLib pick for an ERM.
    static constexpr uint8_t kErmLibraryA = 0x01;
    // Signed full scale in RTP mode: DATA_FORMAT_RTP defaults to signed, so
    // 0x7F is 100% drive in one direction.
    static constexpr uint8_t kRtpFullScale = 0x7F;
    static constexpr uint8_t kAudioMaxDefault = 0x64;

    bool write_register(uint8_t reg, uint8_t value);
    bool read_register(uint8_t reg, uint8_t& out);
    bool update_register(uint8_t reg, uint8_t clear_mask, uint8_t set_mask);

    io::I2c& bus_;
    io::Gpio& gpio_;
    int enable_pin_;
    bool ready_{false};
    bool driving_{false};
    uint8_t device_id_{0};
};

}  // namespace skyblip::parts

#endif
