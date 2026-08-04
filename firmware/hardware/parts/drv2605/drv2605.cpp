#include "hardware/parts/drv2605/drv2605.h"

namespace skyblip::parts {

void Drv2605::power_up() {
    if (enable_pin_ < 0) return;
    gpio_.mode_output(enable_pin_);
    gpio_.set(enable_pin_, true);
}

Status Drv2605::begin() {
    ready_ = false;
    power_up();

    // The address has to answer before a register read means anything: a
    // zero-length write is the bus's own presence question, and on a bus with
    // nothing on it every read would otherwise return the pull-ups' 0xFF.
    if (!bus_.write(kAddress, nullptr, 0)) return Status::Down;

    uint8_t status = 0;
    if (!read_register(kRegStatus, status)) return Status::Down;
    device_id_ = static_cast<uint8_t>(status >> 5);
    switch (device_id_) {
        case 3:  // DRV2605
        case 4:  // DRV2604
        case 5:  // DRV2605X
        case 6:  // DRV2604L
        case 7:  // DRV2605L
            break;
        default: return Status::Unsupported;
    }

    // Out of software standby first: every register below is only writable with
    // the part awake.
    if (!write_register(kRegMode, kModeInternalTrigger)) return Status::Down;
    write_register(kRegRtpInput, 0x00);
    write_register(kRegLibrary, kErmLibraryA);
    write_register(kRegWaveSeq1, 0x01);
    write_register(kRegWaveSeq2, 0x00);  // the terminator: a sequence with no end runs on
    write_register(kRegOverdrive, 0x00);
    write_register(kRegSustainPositive, 0x00);
    write_register(kRegSustainNegative, 0x00);
    write_register(kRegBreak, 0x00);
    write_register(kRegAudioMax, kAudioMaxDefault);
    update_register(kRegFeedback, kFeedbackLraSelect, 0x00);
    update_register(kRegControl3, 0x00, kControl3ErmOpenLoop);

    // Configured and idle. Standby rather than awake: this device counts
    // microamps in a flight bag, and an idle driver stage is milliamps.
    write_register(kRegMode, kModeStandby);
    ready_ = true;
    return Status::Ok;
}

void Drv2605::start() {
    if (!ready_ || driving_) return;
    driving_ = true;
    write_register(kRegMode, kModeRealTimePlayback);
    write_register(kRegRtpInput, kRtpFullScale);
}

void Drv2605::stop() {
    if (!ready_) return;
    driving_ = false;
    write_register(kRegRtpInput, 0x00);
    // GO is cleared too, so a part left mid-sequence by anything else - a reset
    // in the middle of a pulse, a ROM effect from a bench session - is silent
    // when the alarm releases it.
    write_register(kRegGo, 0x00);
    write_register(kRegMode, kModeStandby);
}

void Drv2605::park() {
    stop();
    if (enable_pin_ < 0) return;
    // Released, not driven low: SoftRF makes the same pin an input on the way
    // down (nRF52.cpp:2911) so the rail collapsing does not have a driven output
    // hanging off it.
    gpio_.mode_input(enable_pin_, false);
}

bool Drv2605::write_register(uint8_t reg, uint8_t value) {
    const uint8_t frame[2] = {reg, value};
    return bus_.write(kAddress, frame, sizeof(frame));
}

bool Drv2605::read_register(uint8_t reg, uint8_t& out) {
    if (!bus_.write(kAddress, &reg, 1)) return false;
    return bus_.read(kAddress, &out, 1);
}

bool Drv2605::update_register(uint8_t reg, uint8_t clear_mask, uint8_t set_mask) {
    uint8_t value = 0;
    if (!read_register(reg, value)) return false;
    const uint8_t updated = static_cast<uint8_t>((value & ~clear_mask) | set_mask);
    return write_register(reg, updated);
}

}  // namespace skyblip::parts
