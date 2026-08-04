#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_SUPPLY_MONITOR_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_SUPPLY_MONITOR_H
#if defined(__ZEPHYR__)

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#if defined(CONFIG_NRFX_POWER)
#include <hal/nrf_power.h>
#include <nrfx_power.h>
#endif

namespace skyblip::platform::zephyr {

// The nRF52840's power-failure comparator, POFCON. What it is FOR lives in
// core/power/cutoff.h (may_write): below the warning nothing durable is written
// but the record that must survive, and a fired comparator outranks the divider.
// This is the silicon half, and nothing here decides anything.
//
// WHERE THIS API COMES FROM, because it is not a Zephyr driver and that had to be
// established rather than assumed. Checked against upstream at the exact
// revisions this tree pins (firmware/west.yml: zephyr v4.4.1, whose west.yml
// pins hal_nordic 44fd3d44b15cb75f80a25b4679f91d2787e28664):
//
//   1. Zephyr 4.4 has NO power-failure comparator driver and no binding for one.
//      There is no driver class for it (zephyr/drivers/, zephyr/dts/bindings/),
//      the nRF POWER binding carries reg and interrupts and nothing else
//      (dts/bindings/power/nordic,nrf-power.yaml), and the two Nordic comparator
//      drivers are COMP and LPCOMP (drivers/comparator/comparator_nrf_comp.c,
//      _nrf_lpcomp.c), which are different peripherals on different pins.
//      soc/nordic/nrf52/soc.c touches POWER only for DCDCEN. So nrfx is not a
//      shortcut here, it is the only path.
//   2. nrfx exposes it: nrfx/drivers/include/nrfx_power.h declares
//      nrfx_power_pof_init/enable/disable and nrfx/hal/nrf_power.h the register
//      write under it (nrf_power_pofcon_set, NRF_POWER_POFTHR_V17..V28).
//   3. The interrupt is ALREADY routed. POWER and CLOCK share one IRQ line on
//      this SoC, and drivers/clock_control/clock_control_nrf.c:809 does
//      IRQ_CONNECT(DT_INST_IRQN(0), .., nrfx_isr, nrfx_power_clock_irq_handler),
//      which is nrfx_power.c:423 - it calls nrfx_power_irq_handler() and then
//      nrfx_clock_irq_handler(). nrfx_power_irq_handler dispatches POFWARN to the
//      handler registered by nrfx_power_pof_init. nrfx_clock_enable() enables the
//      NVIC line. So we register a handler; we do not touch an IRQ.
//   4. nrfx_power is already compiled and already initialised in this image:
//      drivers/usb/udc/Kconfig.nrf selects NRFX_POWER, and udc_nrf.c:1769 calls
//      nrfx_power_init() for the USB detect events. POFCON is a second, disjoint
//      user of the same driver - pof_init only stores the handler
//      (nrfx_power.c:172-182) and pof_enable only writes POFCON and INTENSET
//      (nrfx_power.c:184-209), so nothing here re-initialises what USB owns.
//
// The Kconfig is set in products/skyblip_go/prj.conf. It is not #error'd here:
// this header is included by the platform, the platform is included by suites
// that have no reason to carry a USB stack, and a comparator that is not
// compiled in is the absent-capability case the seam already has an answer for -
// armed() says false, the product logs it, and core/power falls back to the
// voltage rule. What must never happen is a SILENT fallback, which is why
// armed() is a read-back of the register and not a copy of the argument.
class SupplyMonitor {
   public:
    // INFO: fc 05aug26 2.8 V, the highest threshold POFCON offers for VDD
    // (nRF52840 PS v1.8 §5.1.3 POFCON.THRESHOLD, V17..V28). Highest on purpose:
    // the earlier the warning the more of it there is, and everything above this
    // voltage is already the divider's business - our own cutoff is a 3.2 V cell
    // and this rail is at or below the cell. What it must not do is fire on a
    // healthy pack, so what the bench has to confirm is that a 22 dBm burst on a
    // 3.4 V cell leaves VDD above 2.8 V; if it does not, the answer is the
    // decoupling, not a lower threshold, because a threshold under 2.8 V is a
    // warning that arrives after the write it was meant to prevent.
    //
    // Whether VDD here is the cell or a regulated 3.3 V rail is a schematic fact
    // this repository does not hold (project/reference/ has no T-Echo Plus
    // schematic), and the choice above is the same either way: 2.8 V is below any
    // healthy cell and above the SoC's own brownout floor (1.7 V).
#if defined(CONFIG_NRFX_POWER)
    static constexpr nrf_power_pof_thr_t kThreshold = NRF_POWER_POFTHR_V28;
#endif

    // Once, at boot. Idempotent: a second call re-reads the register rather than
    // writing it again.
    void arm() {
#if defined(CONFIG_NRFX_POWER)
        if (!armed_) {
            nrfx_power_pofwarn_config_t config = {};
            config.handler = on_warning;
            config.thr = kThreshold;
#if NRFX_POWER_SUPPORTS_POFCON_VDDH
            // The board runs in normal-voltage mode, so this comparator is not the
            // one that can fire; it is set to the register's own reset value
            // rather than to the zero a brace-initialised struct would write.
            config.thrvddh = NRF_POWER_POFTHRVDDH_V27;
#endif
            nrfx_power_pof_init(&config);
            nrfx_power_pof_enable(&config);
        }
        // The proof, not the intent: what POFCON actually holds now.
        bool enabled = false;
        (void)nrf_power_pofcon_get(NRF_POWER, &enabled);
        armed_ = enabled;
#endif
    }

    bool armed() const { return armed_; }

    // Read and cleared. Called from a service pass; the counter it reads is
    // written in interrupt context, which is the whole reason it is atomic.
    bool take_warning() { return atomic_clear(&warnings_) != 0; }

   private:
    // Interrupt context, on the shared POWER/CLOCK line. It does one atomic
    // increment: the decision is core/power's and the pass that polls this is
    // milliseconds away, which is the only budget a collapsing rail gives.
    static void on_warning() { atomic_inc(&warnings_); }

    static inline atomic_t warnings_ = 0;
    bool armed_{false};
};

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
