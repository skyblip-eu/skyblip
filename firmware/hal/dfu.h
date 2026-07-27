// hal/dfu.h — capability port: the firmware-update actions. core/ decides WHEN
// (core/comms/config.cpp gates every one of these on positive-ground plus an
// on-screen confirmation); the adapter decides HOW.
//
// confirm() and enter_recovery() are virtual-with-default rather than pure so a
// board or a test double that only cares about triggering a swap stays valid.
#ifndef SKYBLIP_HAL_DFU_H
#define SKYBLIP_HAL_DFU_H

namespace skyblip::hal {

class Dfu {
   public:
    virtual ~Dfu() = default;

    // Mark the image already staged in the secondary slot for a one-shot swap
    // and reboot. One-shot: if the new image never calls confirm(), the
    // bootloader puts the previous one back on the following boot.
    virtual void trigger() = 0;

    // Declare the running image good, cancelling the pending auto-revert. Must
    // be called by the application only once it is demonstrably working —
    // calling it unconditionally at startup throws away the rollback guarantee,
    // which is the whole point of an A/B layout.
    virtual void confirm() {}

    // Reboot into the factory USB mass-storage bootloader, so a host can
    // drag-and-drop a .uf2. This is the recovery path that survives a bad
    // MCUboot, and the only reason the device never needs a programmer.
    virtual void enter_recovery() {}
};

}  // namespace skyblip::hal

#endif
