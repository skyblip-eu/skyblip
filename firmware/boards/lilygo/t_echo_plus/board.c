// boards/lilygo/t_echo_plus/board.c — bring up the gated peripheral rails.
//
// This lives at BOARD level, not in the application, because MCUboot needs it
// too. `boards/CMakeLists.txt` does add_subdirectory(${BOARD_DIR}) for every
// Zephyr image built for the board, so this SYS_INIT is linked into the
// bootloader as well as into skyBlip.
//
// It has to be: the secondary image slot lives on the external flash, and on
// T-Echo REV_2 and later that part sits behind the same switched rail as the
// e-paper, GNSS and sensors (SoftRF iomap/LilyGO_TEcho.h:80 lists
// "REV_2: FLASH, GNSS, SENSOR" against this pin). Without this, MCUboot reads
// an unpowered flash and every update silently fails to verify.
//
// It also parks the flash's WP# and HOLD# lines high. In quad mode those are
// IO2/IO3 and pinctrl owns them; we drive the part in SINGLE-LINE mode, where
// they are real inputs and a floating one can write-protect the chip or stall a
// transfer. They belong here rather than in the application for the same reason
// the rail does: MCUboot needs them before it touches the secondary slot.
//
// PRE_KERNEL_1 at KERNEL_INIT_PRIORITY_DEVICE (50) is after the nRF GPIO driver
// (GPIO_INIT_PRIORITY defaults to KERNEL_INIT_PRIORITY_DEFAULT, 40) and well
// before the SPI NOR driver, which initialises at POST_KERNEL.
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>

#define RAIL_PORT DEVICE_DT_GET(DT_NODELABEL(gpio0))

enum {
	PERIPHERAL_RAIL_PIN = 12,
	AUX_3V3_RAIL_PIN = 13,
	// External flash WP# (IO2) and HOLD# (IO3), both on gpio0.
	FLASH_WP_PIN = 7,
	FLASH_HOLD_PIN = 5,
};

enum {
	// MX25R1635F tVSL (VCC to device operational), Macronix datasheet rev 1.6;
	// the same 5 ms appears as start_up_time_us in Adafruit_SPIFlash
	// flash_devices.h:219. The ZD25WQ16B alternative specifies less.
	FLASH_STARTUP_US = 5000,
	RAIL_RAMP_US = 5000,
};

static int board_power_up_gated_rails(void)
{
	const struct device *port = RAIL_PORT;

	if (!device_is_ready(port)) {
		return -ENODEV;
	}

	int rc = gpio_pin_configure(port, PERIPHERAL_RAIL_PIN, GPIO_OUTPUT_HIGH);

	if (rc != 0) {
		return rc;
	}

	rc = gpio_pin_configure(port, AUX_3V3_RAIL_PIN, GPIO_OUTPUT_HIGH);
	if (rc != 0) {
		return rc;
	}

	rc = gpio_pin_configure(port, FLASH_WP_PIN, GPIO_OUTPUT_HIGH);
	if (rc != 0) {
		return rc;
	}

	rc = gpio_pin_configure(port, FLASH_HOLD_PIN, GPIO_OUTPUT_HIGH);
	if (rc != 0) {
		return rc;
	}

	k_busy_wait(RAIL_RAMP_US + FLASH_STARTUP_US);

	return 0;
}

SYS_INIT(board_power_up_gated_rails, PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEVICE);
