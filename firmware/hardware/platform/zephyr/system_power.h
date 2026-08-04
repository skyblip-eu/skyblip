#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_SYSTEM_POWER_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_SYSTEM_POWER_H
#if defined(__ZEPHYR__)

#include <hal/nrf_gpio.h>
#include <helpers/nrfx_reset_reason.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/sys/reboot.h>

#include "core/power/shutdown.h"
#include "hal/system_power.h"
#include "hardware/parts/sx1262/sx1262.h"
#include "hardware/platform/zephyr/dfu.h"
#include "hardware/platform/zephyr/supply_monitor.h"

namespace skyblip::platform::zephyr {

// JESD216 / MX25R1635F 8.9 and ZD25WQ16B 8.20, the one opcode both parts spell
// the same: Deep Power-Down.
//
// INFO: fc 03aug26 `has-dpd` is deliberately NOT set on the ext_flash node. It
// would only change behaviour under CONFIG_SPI_NOR_IDLE_IN_DPD or PM_DEVICE, and
// then the driver would have to WAKE the part again - which the two candidate
// parts do differently (the MX25R needs the timed CSn toggles of
// dpd-wakeup-sequence, not RDPD), so a single image would have to be right about
// a chip it cannot identify at build time. We only ever enter DPD, on the way to
// SYSTEM OFF, and the next boot power-cycles the rail, which leaves DPD by
// definition. Nothing has to know how to wake it.
constexpr uint8_t kFlashDeepPowerDown = 0xB9;

// INFO: fc 03aug26 tDP for neither candidate part is in project/reference/, and
// `t-enter-dpd` has no default in jedec,spi-nor-common.yaml, so reading it from
// the devicetree would not compile. Both parts specify it in microseconds; this
// is generous by any reading of that and costs nothing, because the next step
// takes the supply away. Move it to the devicetree the day a datasheet lands.
constexpr uint32_t kFlashEnterDpdUs = 200;

// The part is already parked when this runs (the product sleeps the radio when
// it parks the panel), so BUSY is low and this is one dropped-command insurance
// premium, not a wait for anything. A command issued while BUSY is high is lost,
// and the whole point of re-issuing SetSleep is that this one lands.
constexpr uint32_t kRadioCommandSettleUs = 1000;

class SystemPower : public hal::SystemPower, private power::PowerDownSink {
   public:
    explicit SystemPower(const struct gpio_dt_spec& wake) : wake_(wake) {}

    // Once, at boot. The register latches, so leaving a cause in it makes every
    // later boot report the same reason - and the clear that prevents that is
    // also what makes a second read a lie: an empty register reads as a power-on
    // (see translate), which is exactly the cause the boot path must not confuse
    // a charger with. So the read happens once and is then only remembered.
    void begin() {
        if (!read_) {
            read_ = true;
            causes_ = read_causes();
            hwinfo_clear_reset_cause();
        }
        supply_.arm();
    }

    power::ResetCause reset_causes() const override { return causes_; }

    bool supply_monitor_armed() const override { return supply_.armed(); }
    bool take_supply_warning() override { return supply_.take_warning(); }

    // The order is core/power's, not this file's: everything here is one step
    // of it, and which step happens when is decided and tested on the host.
    void system_off() override {
        // Before anything else, because it is the one step that survives the
        // rails: the factory bootloader must not make its own decision about DFU
        // on the next boot. See kSkipBootloaderMagic for what that buys and what
        // it costs the double-click gesture on exactly one boot. A board with no
        // retention area says so by returning false, and there is nothing to do
        // about it here - it is the same board on which hal::Dfu::enter_recovery
        // cannot work either, and that is the path that reports it.
        (void)write_boot_magic(kSkipBootloaderMagic);
        power::power_down(*this);
        // AFTER the walk above, not before: every step of it reconfigures pins,
        // and the last one arms a level-sensed wake. A DETECT that was latched
        // before or during that is a device that comes straight back up and looks
        // like a flat battery instead of a switched-off one.
        //
        // INFO: fc 05aug26 SoftRF-moshe-braner does the same two writes
        // immediately before SYSTEMOFF, with the reason in the source: "Clear
        // LATCH before sleep to prevent stale DETECT assertion"
        // (src/platform/nRF52.cpp:421-446). Zephyr's own poweroff path does not:
        // sys_poweroff() reaches nrf_power_system_off() and touches neither
        // register.
        clear_stale_latches();

        // INFO: fc 05aug26 There is a second way to reach SYSTEM OFF and it is
        // DELIBERATELY NOT IMPLEMENTED. This is the line a reader meets if the
        // sleep current measured after the sequence above disappoints. MB does not enter
        // SYSTEMOFF from here at all: it writes a magic to GPREGRET2, reboots, and
        // enters SYSTEMOFF from the next boot's power-on peripheral state,
        // explicitly because that is the path that reproduced the low drain they
        // measured (src/platform/nRF52.cpp:2300-2332). It is not free - one
        // power-off becomes a reset plus a boot that has to know not to become a
        // device - so it is worth doing against a number and not before. If the
        // measured current with the sequence above is materially worse than the
        // datasheet's SYSTEM OFF figure, that is the next thing to try.
        sys_poweroff();
    }

    void reboot() override { sys_reboot(SYS_REBOOT_WARM); }

   private:
    static power::ResetCause read_causes() {
        power::ResetCause causes = power::ResetCause::None;
#if NRFX_RESET_REASON_HAS_VBUS
        // Read straight from the register, because Zephyr cannot carry this one:
        // drivers/hwinfo/hwinfo_nrf.c maps NRFX_RESET_REASON_VBUS_MASK onto
        // RESET_POR, so through hwinfo alone a charger plugged into a sleeping
        // device is indistinguishable from a cell being connected for the first
        // time - and core/power/wake.h refuses a boot on the difference. Done
        // before hwinfo_clear_reset_cause(), which clears every bit.
        if (nrfx_reset_reason_get() & NRFX_RESET_REASON_VBUS_MASK)
            causes |= power::ResetCause::UsbVbus;
#endif
        uint32_t flags = 0;
        if (hwinfo_get_reset_cause(&flags) == 0) causes |= translate(flags);
        return causes;
    }

    static void clear_stale_latches() {
#if defined(NRF_GPIO_LATCH_PRESENT)
        uint32_t latched[GPIO_COUNT] = {};
        nrf_gpio_latches_read_and_clear(0, GPIO_COUNT, latched);
#endif
        nrfx_reset_reason_clear(0xFFFFFFFFu);
    }

    void perform(power::PowerDownStep step) override {
        switch (step) {
            case power::PowerDownStep::RadioSleep: radio_sleep(); return;
            case power::PowerDownStep::ExternalFlashDeepPowerDown: flash_deep_power_down(); return;
            case power::PowerDownStep::ExternalFlashLinesReleased:
                // The same three SoftRF releases, in the same place in its own
                // teardown (nRF52.cpp:2914-2916). CS last: it is what latched
                // the command above.
                gpio_pin_configure_dt(&flash_wp_, GPIO_INPUT);
                gpio_pin_configure_dt(&flash_hold_, GPIO_INPUT);
                gpio_pin_configure_dt(&flash_cs_, GPIO_INPUT);
                return;
            case power::PowerDownStep::GnssBackupOff:
                gpio_pin_configure_dt(&gnss_enable_, GPIO_OUTPUT_INACTIVE);
                return;
            case power::PowerDownStep::GnssResetAsserted:
                gpio_pin_configure_dt(&gnss_reset_, GPIO_OUTPUT_ACTIVE);
                return;
            case power::PowerDownStep::RadioResetAsserted:
                gpio_pin_configure_dt(&radio_reset_, GPIO_OUTPUT_ACTIVE);
                return;
            case power::PowerDownStep::PeripheralRailOff:
                gpio_pin_configure_dt(&peripheral_rail_, GPIO_OUTPUT_INACTIVE);
                return;
            case power::PowerDownStep::AuxRailOff:
                gpio_pin_configure_dt(&aux_rail_, GPIO_OUTPUT_INACTIVE);
                return;
            case power::PowerDownStep::DrivenPinsReleased: release_pins(); return;
            case power::PowerDownStep::WakePinArmed: arm_wake(); return;
        }
    }

    // DS 13.1.2 through the same SPI the driver uses, because the SX1262 is not
    // a devicetree device and this object cannot reach parts::Sx1262. Issued
    // again rather than trusted: the product sleeps the radio when it parks the
    // panel, and a falling NSS edge here would wake a sleeping part anyway
    // (DS 9.3), so the second SetSleep is what actually leaves it asleep.
    void radio_sleep() {
        uint8_t frame[2] = {parts::sx::kSetSleep, parts::sx::kSleepWarmStartNoRtc};
        k_busy_wait(kRadioCommandSettleUs);
        write_spi(radio_bus_, radio_cs_, frame, sizeof(frame));
    }

    // While the rail is still up, which is the whole point of where this sits in
    // the order. SoftRF sends the same opcode at the head of its own teardown
    // (nRF52.cpp:2795).
    void flash_deep_power_down() {
        uint8_t frame[1] = {kFlashDeepPowerDown};
        write_spi(flash_bus_, flash_cs_, frame, sizeof(frame));
        k_busy_wait(kFlashEnterDpdUs);
    }

    // Manual CS, matching the drivers: the transfer and the chip select are two
    // separate things on this board.
    static void write_spi(const struct device* bus, const struct gpio_dt_spec& cs,
                          const uint8_t* data, size_t len) {
        if (!device_is_ready(bus)) return;
        gpio_pin_configure_dt(&cs, GPIO_OUTPUT_ACTIVE);
        struct spi_buf buf{const_cast<uint8_t*>(data), len};
        struct spi_buf_set tx{&buf, 1};
        spi_write(bus, &kSpiCfg, &tx);
        gpio_pin_set_dt(&cs, 0);
    }

    // The rails have collapsed by now, so nothing here can back-feed a part
    // through its protection diode. A pull-down on the two enables rather than a
    // bare input: an enable left floating is a rail that may come back.
    void release_pins() {
        k_msleep(power::kRailSettleMs);
        gpio_pin_configure_dt(&peripheral_rail_, GPIO_INPUT | GPIO_PULL_DOWN);
        gpio_pin_configure_dt(&aux_rail_, GPIO_INPUT | GPIO_PULL_DOWN);
        gpio_pin_configure_dt(&gnss_enable_, GPIO_INPUT);
        gpio_pin_configure_dt(&gnss_reset_, GPIO_INPUT);
        gpio_pin_configure_dt(&radio_reset_, GPIO_INPUT);
    }

    void arm_wake() {
        // INFO: hk 02aug26 the nRF52 wakes from SYSTEM OFF on GPIO SENSE, which
        // is a level detect and not an edge. Zephyr's nRF GPIO driver maps
        // GPIO_INT_LEVEL_ACTIVE onto it (samples/boards/nordic/system_off), so
        // this is the wake pin being armed. The caller must already have waited
        // for the button to be released; arming it under a held button wakes the
        // device the instant the rails drop.
        gpio_pin_configure_dt(&wake_, GPIO_INPUT);
        gpio_pin_interrupt_configure_dt(&wake_, GPIO_INT_LEVEL_ACTIVE);
    }

    static power::ResetCause translate(uint32_t flags) {
        // INFO: hk 02aug26 the nRF52 leaves RESETREAS all-zero after a power-on
        // or brown-out reset: it latches only the other seven causes (nRF52840
        // PS v1.8 §5.3.3). An empty register is therefore the power-on case, not
        // an unread one.
        //
        // The other half of that: RESET_POR coming back SET is not a power-on at
        // all on this SoC - it is the VBUS bit, which hwinfo_nrf.c folds into
        // RESET_POR. read_causes() has already reported that as UsbVbus from the
        // register itself, and core/power::classify ranks the specific cause
        // above PowerOn, so the extra bit costs nothing and is left as the driver
        // reported it rather than second-guessed here.
        if (flags == 0) return power::ResetCause::PowerOn;

        power::ResetCause causes = power::ResetCause::None;
        if (flags & RESET_POR) causes |= power::ResetCause::PowerOn;
        if (flags & RESET_PIN) causes |= power::ResetCause::Pin;
        if (flags & RESET_BROWNOUT) causes |= power::ResetCause::Brownout;
        if (flags & RESET_SOFTWARE) causes |= power::ResetCause::Software;
        if (flags & RESET_WATCHDOG) causes |= power::ResetCause::Watchdog;
        if (flags & RESET_CPU_LOCKUP) causes |= power::ResetCause::Lockup;
        if (flags & RESET_LOW_POWER_WAKE) causes |= power::ResetCause::LowPowerWake;
        if (flags & RESET_DEBUG) causes |= power::ResetCause::Debug;
        return causes;
    }

    static constexpr struct spi_config kSpiCfg = {
        .frequency = 8000000U,
        .operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_OP_MODE_MASTER,
        .slave = 0,
        .cs = {},
    };

    struct gpio_dt_spec wake_;
    SupplyMonitor supply_{};
    power::ResetCause causes_{power::ResetCause::None};
    bool read_{false};

    const struct device* radio_bus_{DEVICE_DT_GET(DT_ALIAS(radio_spi))};
    const struct device* flash_bus_{DEVICE_DT_GET(DT_BUS(DT_NODELABEL(ext_flash)))};
    struct gpio_dt_spec radio_cs_ = GPIO_DT_SPEC_GET_BY_IDX(DT_ALIAS(radio_spi), cs_gpios, 0);
    struct gpio_dt_spec flash_cs_ =
        GPIO_DT_SPEC_GET_BY_IDX(DT_BUS(DT_NODELABEL(ext_flash)), cs_gpios, 0);
    struct gpio_dt_spec radio_reset_ = GPIO_DT_SPEC_GET(DT_ALIAS(radio_reset), gpios);
    struct gpio_dt_spec peripheral_rail_ = GPIO_DT_SPEC_GET(DT_ALIAS(peripheral_rail), gpios);
    struct gpio_dt_spec aux_rail_ = GPIO_DT_SPEC_GET(DT_ALIAS(aux_rail), gpios);
    struct gpio_dt_spec flash_wp_ = GPIO_DT_SPEC_GET(DT_ALIAS(flash_wp), gpios);
    struct gpio_dt_spec flash_hold_ = GPIO_DT_SPEC_GET(DT_ALIAS(flash_hold), gpios);
    struct gpio_dt_spec gnss_enable_ = GPIO_DT_SPEC_GET(DT_ALIAS(gnss_enable), gpios);
    struct gpio_dt_spec gnss_reset_ = GPIO_DT_SPEC_GET(DT_ALIAS(gnss_reset), gpios);
};

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
