#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define LCD_WIDTH 160
#define LCD_HEIGHT 80
#define LCD_LINE_COUNT 8
#define LCD_LINE_HEIGHT 9
#define USBFS_BASE 0x50000000U
#define USBFS_GOTGCTL 0x000U
#define USBFS_GUSBCFG 0x00CU
#define USBFS_GCCFG 0x038U
#define USBFS_CID 0x03CU
#define USBFS_GHWCFG3 0x04CU
#define USBFS_GRXFSIZ 0x024U
#define USBFS_GNPTXFSIZ 0x028U
#define USBFS_DIEPTXF1 0x104U
#define USBFS_NUM_IN_EPS 4
#define USBFS_TX_FIFO_COUNT (USBFS_NUM_IN_EPS - 1)
#define USBFS_GCCFG_PWRON BIT(16)
#define USBFS_GCCFG_VBUSBCEN BIT(19)
#define USBFS_GCCFG_VBUSIG BIT(21)
#define RCU_BASE 0x40021000U
#define RCU_CFG0 (RCU_BASE + 0x04U)
#define RCU_AHBEN (RCU_BASE + 0x14U)
#define RCU_RSTSCK (RCU_BASE + 0x24U)
#define RCU_AHBRST (RCU_BASE + 0x28U)
#define RCU_USBFS_BIT BIT(12)
#define RCU_RSTFC BIT(24)
#define TEXT_WIDTH 5
#define TEXT_HEIGHT 7
#define TEXT_ADVANCE 6

struct register_value {
	const char *name;
	uint32_t value;
};

static const struct device *const lcd = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static const struct gpio_dt_spec page_button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static const struct gpio_dt_spec heartbeat_led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const uint8_t slash_glyph[TEXT_HEIGHT] = {1, 1, 2, 4, 8, 16, 16};

static const uint8_t font[36][TEXT_HEIGHT] = {
	{14, 17, 19, 21, 25, 17, 14}, /* 0 */
	{4, 12, 4, 4, 4, 4, 14},
	{14, 17, 1, 2, 4, 8, 31},
	{30, 1, 1, 14, 1, 1, 30},
	{2, 6, 10, 18, 31, 2, 2},
	{31, 16, 16, 30, 1, 1, 30},
	{6, 8, 16, 30, 17, 17, 14},
	{31, 1, 2, 4, 8, 8, 8},
	{14, 17, 17, 14, 17, 17, 14},
	{14, 17, 17, 15, 1, 2, 12},
	{14, 17, 17, 31, 17, 17, 17}, /* A */
	{30, 17, 17, 30, 17, 17, 30},
	{14, 17, 16, 16, 16, 17, 14},
	{28, 18, 17, 17, 17, 18, 28},
	{31, 16, 16, 30, 16, 16, 31},
	{31, 16, 16, 30, 16, 16, 16},
	{14, 17, 16, 23, 17, 17, 15},
	{17, 17, 17, 31, 17, 17, 17},
	{14, 4, 4, 4, 4, 4, 14},
	{7, 2, 2, 2, 2, 18, 12},
	{17, 18, 20, 24, 20, 18, 17},
	{16, 16, 16, 16, 16, 16, 31},
	{17, 27, 21, 21, 17, 17, 17},
	{17, 25, 21, 19, 17, 17, 17},
	{14, 17, 17, 17, 17, 17, 14},
	{30, 17, 17, 30, 16, 16, 16},
	{14, 17, 17, 17, 21, 18, 13},
	{30, 17, 17, 30, 20, 18, 17},
	{15, 16, 16, 14, 1, 1, 30},
	{31, 4, 4, 4, 4, 4, 4},
	{17, 17, 17, 17, 17, 17, 14},
	{17, 17, 17, 17, 17, 10, 4},
	{17, 17, 17, 21, 21, 21, 10},
	{17, 17, 10, 4, 10, 17, 17},
	{17, 17, 10, 4, 4, 4, 4},
	{31, 1, 2, 4, 8, 16, 31},
};

static const uint32_t usbfs_registers[] = {
	[0] = USBFS_GOTGCTL,
	[1] = USBFS_GUSBCFG,
	[2] = USBFS_GCCFG,
	[3] = USBFS_CID,
	[4] = USBFS_GHWCFG3,
	[5] = USBFS_GRXFSIZ,
	[6] = USBFS_GNPTXFSIZ,
};

static const char *const usbfs_register_names[] = {
	"GOTGCTL", "GUSBCFG", "GCCFG", "CID",
	"GHWCFG3", "GRXFSIZ", "GNPTXFSIZ",
};

static uint32_t usbfs_values[ARRAY_SIZE(usbfs_registers)];
static uint32_t tx_fifo_values[USBFS_TX_FIFO_COUNT];
static uint32_t rcu_cfg0_value;
static uint32_t rcu_ahben_value;
static uint32_t rcu_ahbrst_value;
static uint32_t rcu_rstsck_value;
static uint32_t heartbeat_last_toggle;
static bool heartbeat_led_on;
static uint16_t display_row[LCD_WIDTH];
static char display_lines[LCD_LINE_COUNT][32];

static const uint8_t *get_glyph(char ch)
{
	if (ch >= '0' && ch <= '9') {
		return font[(size_t)(ch - '0')];
	}
	if (ch >= 'A' && ch <= 'Z') {
		return font[10U + (size_t)(ch - 'A')];
	}
	if (ch == '/') {
		return slash_glyph;
	}
	return NULL;
}

static bool pixel_is_set(const char *text, int x, int y)
{
	int char_index = x / TEXT_ADVANCE;
	int glyph_x = x % TEXT_ADVANCE;
	const uint8_t *glyph;

	if (glyph_x >= TEXT_WIDTH || text[char_index] == '\0') {
		return false;
	}
	glyph = get_glyph(text[char_index]);
	return glyph != NULL && (glyph[y] & BIT(TEXT_WIDTH - 1 - glyph_x)) != 0;
}

static int draw_lines(const char lines[LCD_LINE_COUNT][32])
{
	const struct display_buffer_descriptor desc = {
		.buf_size = sizeof(display_row),
		.width = LCD_WIDTH,
		.height = 1,
		.pitch = LCD_WIDTH,
	};

	for (int y = 0; y < LCD_HEIGHT; y++) {
		memset(display_row, 0, sizeof(display_row));
		for (size_t line = 0; line < LCD_LINE_COUNT; line++) {
			int line_y = (int)line * LCD_LINE_HEIGHT;

			if (y < line_y || y >= line_y + TEXT_HEIGHT) {
				continue;
			}
			size_t text_len = strlen(lines[line]);
			int text_width = (int)text_len * TEXT_ADVANCE - 1;
			int start_x = MAX(0, (LCD_WIDTH - text_width) / 2);

			for (int x = 0; x < text_width && start_x + x < LCD_WIDTH; x++) {
				if (pixel_is_set(lines[line], x, y - line_y)) {
					display_row[start_x + x] = UINT16_MAX;
				}
			}
		}
		int ret = display_write(lcd, 0, y, &desc, display_row);

		if (ret < 0) {
			return ret;
		}
	}
	return 0;
}

static int draw_button_status(int state)
{
	char text[16];
	size_t text_width;
	int start_x;

	snprintk(text, sizeof(text), "BOOT0 %u", (unsigned int)(state != 0));
	text_width = strlen(text) * TEXT_ADVANCE - 1U;
	start_x = (LCD_WIDTH - (int)text_width) / 2;

	for (int y = 0; y < TEXT_HEIGHT; y++) {
		memset(display_row, 0, sizeof(display_row));
		for (size_t x = 0; x < text_width; x++) {
			if (pixel_is_set(text, x, y)) {
				display_row[start_x + x] = UINT16_MAX;
			}
		}
		int ret = display_write(lcd, 0, LCD_HEIGHT - TEXT_HEIGHT - 1 + y,
					&(const struct display_buffer_descriptor) {
						.buf_size = sizeof(display_row),
						.width = LCD_WIDTH,
						.height = 1,
						.pitch = LCD_WIDTH,
					},
					display_row);

		if (ret < 0) {
			return ret;
		}
	}
	return 0;
}

static void set_line(char lines[LCD_LINE_COUNT][32], size_t line, const char *format,
		     const char *name, uint32_t value)
{
	snprintk(lines[line], sizeof(lines[line]), format, name, value);
}

static void collect_registers(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(usbfs_registers); i++) {
		usbfs_values[i] = sys_read32(USBFS_BASE + usbfs_registers[i]);
	}
	rcu_cfg0_value = sys_read32(RCU_CFG0);
	rcu_ahben_value = sys_read32(RCU_AHBEN);
	rcu_ahbrst_value = sys_read32(RCU_AHBRST);
	rcu_rstsck_value = sys_read32(RCU_RSTSCK);
	sys_write32(rcu_rstsck_value | RCU_RSTFC, RCU_RSTSCK);

	for (size_t i = 0; i < ARRAY_SIZE(tx_fifo_values); i++) {
		tx_fifo_values[i] = sys_read32(USBFS_BASE + USBFS_DIEPTXF1 + (i * 4U));
	}
}

static void report_registers(void)
{
	printk("USBFS register diagnostic (read-only)\n");
	printk("RCU CFG0=%08x AHBEN=%08x AHBRST=%08x\n",
	       rcu_cfg0_value, rcu_ahben_value, rcu_ahbrst_value);
	printk("RCU RSTSCK=%08x\n", rcu_rstsck_value);
	printk("Reset flags: POR=%u SW=%u FWDGT=%u WWDGT=%u LP=%u\n",
	       (rcu_rstsck_value >> 27) & 1U,
	       (rcu_rstsck_value >> 28) & 1U,
	       (rcu_rstsck_value >> 29) & 1U,
	       (rcu_rstsck_value >> 30) & 1U,
	       (rcu_rstsck_value >> 31) & 1U);
	printk("USBFS clock divider field=%u, AHB clock=%s\n",
	       (rcu_cfg0_value >> 22) & 0x3U,
	       (rcu_ahben_value & RCU_USBFS_BIT) ? "enabled" : "disabled");
	for (size_t i = 0; i < ARRAY_SIZE(usbfs_values); i++) {
		printk("%s=%08x\n", usbfs_register_names[i], usbfs_values[i]);
	}
	printk("DIEPTXF count=%u\n", USBFS_TX_FIFO_COUNT);
	for (size_t i = 0; i < ARRAY_SIZE(tx_fifo_values); i++) {
		printk("DIEPTXF%u=%08x\n", (unsigned int)(i + 1U), tx_fifo_values[i]);
	}
}

static void make_page(size_t page, char lines[LCD_LINE_COUNT][32], size_t fifo_pages)
{
	memset(lines, 0, LCD_LINE_COUNT * 32U);
	if (page == 0U) {
		snprintk(lines[0], sizeof(lines[0]), "USBFS DWC2");
		for (size_t i = 0; i < ARRAY_SIZE(usbfs_values); i++) {
			set_line(lines, i + 1U, "%s %08X",
				 usbfs_register_names[i], usbfs_values[i]);
		}
		return;
	}
	if (page == 1U) {
#ifdef LONGAN_NANO_USB_CLOCK_TEST
		snprintk(lines[0], sizeof(lines[0]), "USB 48M TEST");
#else
		snprintk(lines[0], sizeof(lines[0]), "CLOCK STATE");
#endif
		snprintk(lines[1], sizeof(lines[1]), "RCU CFG0 %08X", rcu_cfg0_value);
		snprintk(lines[2], sizeof(lines[2]), "RCU AHBEN %08X", rcu_ahben_value);
		snprintk(lines[3], sizeof(lines[3]), "RCU AHBRST %08X", rcu_ahbrst_value);
		snprintk(lines[4], sizeof(lines[4]), "USB DIV %u",
			 (rcu_cfg0_value >> 22) & 0x3U);
		snprintk(lines[5], sizeof(lines[5]), "USB AHB %s",
			 (rcu_ahben_value & RCU_USBFS_BIT) ? "ON" : "OFF");
		snprintk(lines[6], sizeof(lines[6]), "RCU RSTSCK %08X", rcu_rstsck_value);
		return;
	}

	size_t fifo_page = page - 2U;
	size_t first_fifo = fifo_page * (LCD_LINE_COUNT - 1U);

	snprintk(lines[0], sizeof(lines[0]), "TX FIFO %u/%u",
		 (unsigned int)(fifo_page + 1U), (unsigned int)fifo_pages);
	for (size_t row = 1; row < LCD_LINE_COUNT; row++) {
		size_t fifo = first_fifo + row - 1U;

		if (fifo >= ARRAY_SIZE(tx_fifo_values)) {
			break;
		}
		snprintk(lines[row], sizeof(lines[row]), "DIEPTXF%02u %08X",
			 (unsigned int)(fifo + 1U), tx_fifo_values[fifo]);
	}
}

static void enable_usbfs_clock(void)
{
	uint32_t ahben = sys_read32(RCU_AHBEN);
	uint32_t ahbrst;

	sys_write32(ahben | RCU_USBFS_BIT, RCU_AHBEN);
	ahbrst = sys_read32(RCU_AHBRST);
	sys_write32(ahbrst | RCU_USBFS_BIT, RCU_AHBRST);
	sys_write32(ahbrst & ~RCU_USBFS_BIT, RCU_AHBRST);
}

static void power_usbfs_core(void)
{
	uint32_t gccfg = sys_read32(USBFS_BASE + USBFS_GCCFG);

	gccfg |= USBFS_GCCFG_PWRON | USBFS_GCCFG_VBUSIG | USBFS_GCCFG_VBUSBCEN;
	sys_write32(gccfg, USBFS_BASE + USBFS_GCCFG);
}

static int update_heartbeat(void)
{
	uint32_t now = k_uptime_get_32();

	if ((uint32_t)(now - heartbeat_last_toggle) < 500U) {
		return 0;
	}
	heartbeat_led_on = !heartbeat_led_on;
	heartbeat_last_toggle = now;
	return gpio_pin_set_dt(&heartbeat_led, heartbeat_led_on);
}

static int wait_for_page_button(void)
{
	int state;
	int displayed_state = -1;

	while (true) {
		int ret = update_heartbeat();

		if (ret < 0) {
			return ret;
		}
		state = gpio_pin_get_dt(&page_button);
		if (state < 0) {
			return state;
		}
		if (state != displayed_state) {
			int ret = draw_button_status(state);

			if (ret < 0) {
				return ret;
			}
			displayed_state = state;
		}
		if (state != 0) {
			k_sleep(K_MSEC(30));
			state = gpio_pin_get_dt(&page_button);
			if (state < 0) {
				return state;
			}
			if (state != 0) {
				break;
			}
		}
		k_sleep(K_MSEC(20));
	}

	do {
		k_sleep(K_MSEC(20));
		int ret = update_heartbeat();

		if (ret < 0) {
			return ret;
		}
		state = gpio_pin_get_dt(&page_button);
		if (state < 0) {
			return state;
		}
		if (state != displayed_state) {
			int ret = draw_button_status(state);

			if (ret < 0) {
				return ret;
			}
			displayed_state = state;
		}
	} while (state != 0);

	k_sleep(K_MSEC(100));
	return 0;
}

int main(void)
{
	struct display_capabilities caps;
	size_t fifo_pages;
	size_t page_count;
	size_t page = 0;
	int ret;

	if (!device_is_ready(lcd)) {
		printk("LCD is not ready\n");
		return -ENODEV;
	}
	display_get_capabilities(lcd, &caps);
	if (caps.x_resolution != LCD_WIDTH || caps.y_resolution != LCD_HEIGHT) {
		printk("Unexpected LCD resolution: %u x %u\n",
		       caps.x_resolution, caps.y_resolution);
		return -ENOTSUP;
	}
	if (!gpio_is_ready_dt(&page_button)) {
		printk("BOOT0 button GPIO is not ready\n");
		return -ENODEV;
	}
	ret = gpio_pin_configure_dt(&page_button, GPIO_INPUT | GPIO_PULL_DOWN);
	if (ret < 0) {
		printk("BOOT0 button setup failed: %d\n", ret);
		return ret;
	}
	if (!gpio_is_ready_dt(&heartbeat_led)) {
		printk("Heartbeat LED GPIO is not ready\n");
		return -ENODEV;
	}
	ret = gpio_pin_configure_dt(&heartbeat_led, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		printk("Heartbeat LED setup failed: %d\n", ret);
		return ret;
	}
	ret = gpio_pin_set_dt(&heartbeat_led, 1);
	if (ret < 0) {
		printk("Heartbeat LED initial state failed: %d\n", ret);
		return ret;
	}
	heartbeat_led_on = true;
	heartbeat_last_toggle = k_uptime_get_32();

	enable_usbfs_clock();
	k_busy_wait(10);
	power_usbfs_core();
	k_busy_wait(10);
	collect_registers();
	report_registers();

	if (caps.current_pixel_format != PIXEL_FORMAT_RGB_565 &&
	    caps.current_pixel_format != PIXEL_FORMAT_RGB_565X) {
		printk("Unsupported LCD pixel format\n");
		return -ENOTSUP;
	}
	ret = display_blanking_off(lcd);

	if (ret < 0) {
		printk("LCD unblank failed: %d\n", ret);
		return ret;
	}

	fifo_pages = MAX(1U, (USBFS_TX_FIFO_COUNT + LCD_LINE_COUNT - 2U) /
				 (LCD_LINE_COUNT - 1U));
	page_count = 2U + fifo_pages;

	while (true) {
		make_page(page, display_lines, fifo_pages);
		ret = draw_lines(display_lines);
		if (ret < 0) {
			printk("LCD draw failed: %d\n", ret);
			return ret;
		}
		printk("Displayed page %u/%u. Press and release BOOT0 to continue.\n",
		       (unsigned int)(page + 1U), (unsigned int)page_count);
		ret = wait_for_page_button();
		if (ret < 0) {
			printk("BOOT0 button read failed: %d\n", ret);
			return ret;
		}
		page = (page + 1U) % page_count;
	}
}
