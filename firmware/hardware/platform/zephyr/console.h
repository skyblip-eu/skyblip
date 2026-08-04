#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_CONSOLE_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_CONSOLE_H
#if defined(__ZEPHYR__)

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>

namespace skyblip::platform::zephyr {

// Where a status dump leaves the device: the USB CDC console prj.conf already
// configures (zephyr,console = &cdc_acm_uart0). Lines only, no shell and no input
// - there is nothing on this device a laptop is allowed to command that the
// companion link does not already gate behind a button press on the glass.
//
// Two properties matter and both are the reason this is not four lines inline in
// main.cpp:
//
//  - It writes NOTHING until a host has raised DTR. CDC ACM is a UART with line
//    control (CONFIG_UART_LINE_CTRL), so "is anyone there" is a real question with
//    a real answer, and a device in a flight bag must not spend a milliamp
//    formatting five lines for nobody. A plain UART console has no line control
//    and is always attached, which is what the -ENOSYS case means.
//  - It goes through printk, which CONFIG_LOG routes into the deferred log
//    processing (LOG_PRINTK), so a host that opens the port and then stops reading
//    drops bytes instead of blocking the caller. This is called from the service
//    pass: a dump that blocked there would cost the watchdog, and the radio
//    executor runs above it precisely so that a dump can never cost a dwell.
class Console {
   public:
    bool ready() const {
        const struct device* dev = console_device();
        if (dev == nullptr || !device_is_ready(dev)) return false;
        uint32_t dtr = 0;
        const int err = uart_line_ctrl_get(dev, UART_LINE_CTRL_DTR, &dtr);
        if (err == -ENOSYS || err == -ENOTSUP) return true;
        if (err != 0) return false;
        return dtr != 0;
    }

    // text is NUL-terminated by the formatter (core/comms/diagnostics.h), which
    // also bounds its length; len is passed for the sinks that want it.
    void line(const char* text, int /*len*/) { printk("%s\n", text); }

   private:
    static const struct device* console_device() {
#if DT_HAS_CHOSEN(zephyr_console)
        return DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
#else
        return nullptr;
#endif
    }
};

}  // namespace skyblip::platform::zephyr

#endif
#endif
