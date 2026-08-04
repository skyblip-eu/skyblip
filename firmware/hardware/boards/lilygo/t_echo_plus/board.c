// boards/lilygo/t_echo_plus/board.c: bring up the gated peripheral rails.
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
// The L76K's own two lines are raised here as well. They are not MCUboot's
// business, but they are the board's: the receiver's reset is active low with
// no external pull, so leaving the pin floating leaves the part undefined.
// SoftRF drives reset then wake high for REV_2 and the Plus
// (platform/nRF52.cpp:1589-1593).
//
// INFO: fc 06aug26 P1.05 and P1.02 agree across SoftRF's iomap and meshcore's
// variant for this board, and neither has been read on ours. This is the one
// change on this branch that can make a working unit stop working: it should be
// strictly better than a floating reset line, so if a bench unit comes up with no
// receiver, these two pins are the first suspects.
//
// PRE_KERNEL_1 at KERNEL_INIT_PRIORITY_DEVICE (50) is after the nRF GPIO driver
// (GPIO_INIT_PRIORITY defaults to KERNEL_INIT_PRIORITY_DEFAULT, 40) and well
// before the SPI NOR driver, which initialises at POST_KERNEL.
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <string.h>

enum {
	// MX25R1635F tVSL (VCC to device operational), Macronix datasheet rev 1.6;
	// the same 5 ms appears as start_up_time_us in Adafruit_SPIFlash
	// flash_devices.h:219. The ZD25WQ16B alternative specifies less.
	FLASH_STARTUP_US = 5000,
	RAIL_RAMP_US = 5000,
};

// Every pin below is declared once, in the board devicetree's
// board_power_gpios node, so this file and the shutdown path cannot disagree
// about which pin is which.
static const struct gpio_dt_spec board_power_pins[] = {
	GPIO_DT_SPEC_GET(DT_ALIAS(peripheral_rail), gpios),
	GPIO_DT_SPEC_GET(DT_ALIAS(aux_rail), gpios),
	GPIO_DT_SPEC_GET(DT_ALIAS(flash_wp), gpios),
	GPIO_DT_SPEC_GET(DT_ALIAS(flash_hold), gpios),
};

// Raised after the rail, because the receiver has to have a supply before it is
// let out of reset. Reset first, then wake, which is SoftRF's order.
static const struct gpio_dt_spec gnss_reset = GPIO_DT_SPEC_GET(DT_ALIAS(gnss_reset), gpios);
static const struct gpio_dt_spec gnss_enable = GPIO_DT_SPEC_GET(DT_ALIAS(gnss_enable), gpios);

// Defined below: the two things this board has to read off its own pins before
// any driver claims them. A no-op in MCUboot.
static void board_probe_fitted_parts(void);

static int board_power_up_gated_rails(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(board_power_pins); i++) {
		if (!gpio_is_ready_dt(&board_power_pins[i])) {
			return -ENODEV;
		}

		// GPIO_ACTIVE_HIGH throughout, so ACTIVE is the pin high.
		int rc = gpio_pin_configure_dt(&board_power_pins[i], GPIO_OUTPUT_ACTIVE);

		if (rc != 0) {
			return rc;
		}
	}

	k_busy_wait(RAIL_RAMP_US + FLASH_STARTUP_US);

	if (!gpio_is_ready_dt(&gnss_reset) || !gpio_is_ready_dt(&gnss_enable)) {
		return -ENODEV;
	}

	// The reset line is GPIO_ACTIVE_LOW in the devicetree, so INACTIVE releases
	// the part; the wake line is active high.
	int rc = gpio_pin_configure_dt(&gnss_reset, GPIO_OUTPUT_INACTIVE);

	if (rc != 0) {
		return rc;
	}

	rc = gpio_pin_configure_dt(&gnss_enable, GPIO_OUTPUT_ACTIVE);

	if (rc != 0) {
		return rc;
	}

	// Last, and in this function rather than in a SYS_INIT of its own: the panel
	// cannot be asked anything before it has a supply, and one init entry cannot
	// be reordered against another by a priority arithmetic mistake.
	board_probe_fitted_parts();
	return 0;
}

SYS_INIT(board_power_up_gated_rails, PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEVICE);

// ---------------------------------------------------------------------------
// The two questions only this file can ask.
//
// LilyGO fits more than one part behind the same footprint, so two facts have to
// be READ rather than declared: which e-paper is glued on, and whether the
// buzzer pin is capable of swinging anything. Both need pins that a Zephyr
// driver owns from POST_KERNEL onwards - the e-paper fingerprint is clocked out
// over MOSI with the line reversed (this board's SPI MISO is a placeholder: the
// panel is write-only in normal operation), and the buzzer pin has to be read
// high-Z while the PWM peripheral is not driving it. PRE_KERNEL_1 is the one
// moment both are true, which is why this lives at board level and not in the
// panel driver: a driver that could only be called during board init would be a
// driver nothing could call.
//
// Read once, into two words the application asks for later
// (hardware/platform/zephyr/platform.h declares both as board hooks).
//
// Skipped in MCUboot: the bootloader has no use for either answer and no reason
// to pay a quarter of a second for them.
// ---------------------------------------------------------------------------
#if !defined(CONFIG_MCUBOOT)

enum {
	// SoftRF's own ident sequence: reset low for 20 ms, released, then 200 ms
	// before the panel will answer (platform/nRF52.cpp:3785-3792). It doubles as
	// the panel's power-on settle, which nothing else on this board pays for.
	PANEL_RESET_LOW_US = 20000,
	PANEL_SETTLE_US = 200000,
	// Half a bit at about 500 kHz. The panel's controller has no minimum clock.
	PANEL_CLOCK_HALF_US = 1,
	// The internal pull-up is ~13 kOhm (nRF52840 PS v1.8, GPIO electrical
	// specification). A 9x9 mm SMD piezo is tens of nanofarads, so 1 ms is
	// several time constants and a pin that still reads low is held low by
	// something. SoftRF waits the same 1 ms (platform/nRF52.cpp:1271).
	BUZZER_PULLUP_SETTLE_US = 1000,
	// Between the two registers, as the reference does (delay(1)).
	PANEL_INTER_REGISTER_US = 1000,
	PANEL_ID_A_BYTES = 11,
	PANEL_ID_B_BYTES = 10,
};

#define PANEL_ID_REGISTER_A 0x2D
#define PANEL_ID_REGISTER_B 0x2E
#define PANEL_FINGERPRINT_BYTES (PANEL_ID_A_BYTES + PANEL_ID_B_BYTES)

static const struct gpio_dt_spec epd_cs =
	GPIO_DT_SPEC_GET_BY_IDX(DT_ALIAS(epd_spi), cs_gpios, 0);
static const struct gpio_dt_spec epd_dc = GPIO_DT_SPEC_GET(DT_NODELABEL(epd_dc_gpio), gpios);
static const struct gpio_dt_spec epd_reset = GPIO_DT_SPEC_GET(DT_NODELABEL(epd_reset_gpio), gpios);
static const struct gpio_dt_spec epd_busy = GPIO_DT_SPEC_GET(DT_NODELABEL(epd_busy_gpio), gpios);
static const struct gpio_dt_spec epd_mosi = GPIO_DT_SPEC_GET(DT_NODELABEL(epd_mosi_gpio), gpios);
static const struct gpio_dt_spec epd_sck = GPIO_DT_SPEC_GET(DT_NODELABEL(epd_sck_gpio), gpios);
static const struct gpio_dt_spec buzzer_sense =
	GPIO_DT_SPEC_GET(DT_NODELABEL(buzzer_sense_gpio), gpios);

static uint8_t panel_fingerprint[PANEL_FINGERPRINT_BYTES];
static bool panel_fingerprint_read;
static bool buzzer_pin_low_against_pullup;

// Mode 0, MSB first, half duplex: the panel's one data line is the host's MOSI,
// so the same pin carries the command out and the answer back.
static void panel_clock_out(uint8_t value)
{
	for (int bit = 7; bit >= 0; bit--) {
		gpio_pin_set_dt(&epd_mosi, (value >> bit) & 1);
		gpio_pin_set_dt(&epd_sck, 1);
		k_busy_wait(PANEL_CLOCK_HALF_US);
		gpio_pin_set_dt(&epd_sck, 0);
		k_busy_wait(PANEL_CLOCK_HALF_US);
	}
}

static uint8_t panel_clock_in(void)
{
	uint8_t value = 0;

	for (int bit = 7; bit >= 0; bit--) {
		gpio_pin_set_dt(&epd_sck, 1);
		k_busy_wait(PANEL_CLOCK_HALF_US);
		if (gpio_pin_get_dt(&epd_mosi) == 1) {
			value |= (uint8_t)(1u << bit);
		}
		gpio_pin_set_dt(&epd_sck, 0);
		k_busy_wait(PANEL_CLOCK_HALF_US);
	}

	return value;
}

static void panel_read_register(uint8_t reg, uint8_t *out, size_t len)
{
	gpio_pin_configure_dt(&epd_mosi, GPIO_OUTPUT_INACTIVE);
	gpio_pin_set_dt(&epd_dc, 0); /* command */
	gpio_pin_set_dt(&epd_cs, 1); /* active low in the devicetree: 1 selects */

	panel_clock_out(reg);

	gpio_pin_configure_dt(&epd_mosi, GPIO_INPUT);
	gpio_pin_set_dt(&epd_dc, 1); /* data */

	for (size_t i = 0; i < len; i++) {
		out[i] = panel_clock_in();
	}

	gpio_pin_set_dt(&epd_sck, 0);
	gpio_pin_set_dt(&epd_dc, 0);
	gpio_pin_set_dt(&epd_cs, 0);
}

// No critical section, unlike the reference, which wraps each register in
// taskENTER_CRITICAL: at PRE_KERNEL_1 there is no scheduler to be preempted by.
static void board_read_panel_fingerprint(void)
{
	if (!gpio_is_ready_dt(&epd_cs) || !gpio_is_ready_dt(&epd_dc) ||
	    !gpio_is_ready_dt(&epd_mosi) || !gpio_is_ready_dt(&epd_sck)) {
		return;
	}

	gpio_pin_configure_dt(&epd_cs, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&epd_dc, GPIO_OUTPUT_ACTIVE);
	gpio_pin_configure_dt(&epd_sck, GPIO_OUTPUT_INACTIVE);

	// The panel's reset is active low and the devicetree declares the pin
	// ACTIVE_HIGH, because the display driver drives raw levels through io::Gpio.
	// So INACTIVE here is the pin low, which is the part held in reset.
	gpio_pin_configure_dt(&epd_reset, GPIO_OUTPUT_INACTIVE);
	k_busy_wait(PANEL_RESET_LOW_US);
	// Released to a pull-up rather than driven high, which is what the reference
	// does: the line is shared with nothing, and the panel's own pull decides.
	gpio_pin_configure_dt(&epd_reset, GPIO_INPUT | GPIO_PULL_UP);
	k_busy_wait(PANEL_SETTLE_US);
	gpio_pin_configure_dt(&epd_busy, GPIO_INPUT);

	panel_read_register(PANEL_ID_REGISTER_A, panel_fingerprint, PANEL_ID_A_BYTES);
	k_busy_wait(PANEL_INTER_REGISTER_US);
	panel_read_register(PANEL_ID_REGISTER_B, panel_fingerprint + PANEL_ID_A_BYTES,
			    PANEL_ID_B_BYTES);

	panel_fingerprint_read = true;
}

// High-Z, then against the internal pull-up. A pin that reads low both times is
// held low by the board and cannot drive a transducer. It is the reading SoftRF
// uses to identify a board whose buzzer stage pulls the pin down
// (platform/nRF52.cpp:1265-1275); on this board, where a passive piezo hangs off
// the pin directly, it can only ever CONTRADICT a fitted buzzer, never confirm
// one - an empty pad and a charged piezo read the same.
static void board_probe_buzzer_pin(void)
{
	if (!gpio_is_ready_dt(&buzzer_sense)) {
		return;
	}

	gpio_pin_configure_dt(&buzzer_sense, GPIO_INPUT);
	const int high_impedance = gpio_pin_get_dt(&buzzer_sense);

	gpio_pin_configure_dt(&buzzer_sense, GPIO_INPUT | GPIO_PULL_UP);
	k_busy_wait(BUZZER_PULLUP_SETTLE_US);
	const int against_pullup = gpio_pin_get_dt(&buzzer_sense);

	// Left as a plain input: pwm0's pinctrl takes the pin at POST_KERNEL.
	gpio_pin_configure_dt(&buzzer_sense, GPIO_INPUT);

	buzzer_pin_low_against_pullup = high_impedance == 0 && against_pullup == 0;
}

static void board_probe_fitted_parts(void)
{
	board_read_panel_fingerprint();
	board_probe_buzzer_pin();
}

size_t board_panel_fingerprint(uint8_t *out, size_t capacity)
{
	if (!panel_fingerprint_read || capacity < PANEL_FINGERPRINT_BYTES) {
		return 0;
	}

	memcpy(out, panel_fingerprint, PANEL_FINGERPRINT_BYTES);
	return PANEL_FINGERPRINT_BYTES;
}

int board_buzzer_pin_held_low(void)
{
	return buzzer_pin_low_against_pullup ? 1 : 0;
}

#else

static void board_probe_fitted_parts(void)
{
}

size_t board_panel_fingerprint(uint8_t *out, size_t capacity)
{
	(void)out;
	(void)capacity;
	return 0;
}

int board_buzzer_pin_held_low(void)
{
	return 0;
}

#endif /* !CONFIG_MCUBOOT */
