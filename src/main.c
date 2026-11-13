#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#if defined(CONFIG_CDC_ACM_SERIAL_INITIALIZE_AT_BOOT)
#include <zephyr/drivers/uart.h>
#endif

#if DT_NODE_EXISTS(DT_NODELABEL(sim_led_display))
#include <zephyr/drivers/gpio/gpio_emul.h>
#endif

#define LCD_WIDTH 160
#define LCD_HEIGHT 80

static const struct gpio_dt_spec leds[] = {
	GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios),
	GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios),
	GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios),
};

static const struct device *const lcd = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

int app_lcd_init(void)
{
	struct display_capabilities caps;

	if (!device_is_ready(lcd)) {
		return -ENODEV;
	}
	display_get_capabilities(lcd, &caps);
	if (caps.x_resolution != LCD_WIDTH || caps.y_resolution != LCD_HEIGHT ||
	    (caps.current_pixel_format != PIXEL_FORMAT_RGB_565 &&
	     caps.current_pixel_format != PIXEL_FORMAT_RGB_565X)) {
		return -ENOTSUP;
	}
	return 0;
}

int app_lcd_write_row(uint16_t y, const uint16_t *pixels)
{
	const struct display_buffer_descriptor desc = {
		.buf_size = LCD_WIDTH * sizeof(*pixels),
		.width = LCD_WIDTH,
		.height = 1,
		.pitch = LCD_WIDTH,
	};

	return display_write(lcd, 0, y, &desc, pixels);
}

int app_lcd_show(void)
{
	return display_blanking_off(lcd);
}

int app_leds_init(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(leds); i++) {
		if (!gpio_is_ready_dt(&leds[i])) {
			return -ENODEV;
		}
		int ret = gpio_pin_configure_dt(&leds[i], GPIO_OUTPUT_INACTIVE);

		if (ret < 0) {
			return ret;
		}
	}
	return 0;
}

int app_led_set(uint32_t index, bool active)
{
	if (index >= ARRAY_SIZE(leds)) {
		return -EINVAL;
	}
	return gpio_pin_set_dt(&leds[index], active);
}

#if DT_NODE_EXISTS(DT_NODELABEL(sim_led_display))
static const struct device *const led_display = DEVICE_DT_GET(DT_NODELABEL(sim_led_display));

int app_sim_led_init(void)
{
	if (!device_is_ready(led_display)) {
		return -ENODEV;
	}
	return display_blanking_off(led_display);
}

int app_sim_led_update(void)
{
	static uint16_t pixels[32 * 32];
	static const uint16_t colors[] = {0xf800, 0x07e0, 0x001f};
	uint16_t color = 0;
	const struct display_buffer_descriptor desc = {
		.buf_size = sizeof(pixels),
		.width = 32,
		.height = 32,
		.pitch = 32,
	};

	for (size_t i = 0; i < ARRAY_SIZE(leds); i++) {
		int state = gpio_emul_output_get_dt(&leds[i]);

		if (state < 0) {
			return state;
		}
		if (state != 0) {
			color |= colors[i];
		}
	}
	for (size_t i = 0; i < ARRAY_SIZE(pixels); i++) {
		pixels[i] = color;
	}
	return display_write(led_display, 0, 0, &desc, pixels);
}
#else
int app_sim_led_init(void)
{
	return 0;
}

int app_sim_led_update(void)
{
	return 0;
}
#endif

void app_sleep_one_second(void)
{
	k_sleep(K_SECONDS(1));
}

void app_report_error(const char *operation, int status)
{
	printk("%s failed: %d\n", operation, status);
}

void app_report_message(const char *message)
{
	printk("%s\n", message);
}

#if defined(CONFIG_CDC_ACM_SERIAL_INITIALIZE_AT_BOOT)
static struct k_work_delayable greeting_work;
static const struct device *const console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
static bool console_connected;

static void report_usb_greeting(struct k_work *work)
{
	uint32_t dtr;
	int ret;

	ARG_UNUSED(work);
	ret = uart_line_ctrl_get(console, UART_LINE_CTRL_DTR, &dtr);
	if (ret < 0) {
		printk("USB console DTR check failed: %d\n", ret);
		return;
	}
	if (dtr && console_connected) {
		printk("Hello, Zephyr World (%s)\n", CONFIG_BOARD_TARGET);
		return;
	}
	console_connected = dtr;
	/* Allow the CDC transmit FIFO to drain the earlier boot banner. */
	ret = k_work_schedule(&greeting_work, K_MSEC(dtr ? 250 : 100));
	if (ret < 0) {
		printk("USB console greeting scheduling failed: %d\n", ret);
	}
}
#endif

void app_report_greeting(void)
{
#if defined(CONFIG_CDC_ACM_SERIAL_INITIALIZE_AT_BOOT)
	int ret;

	k_work_init_delayable(&greeting_work, report_usb_greeting);
	ret = k_work_schedule(&greeting_work, K_NO_WAIT);
	if (ret < 0) {
		printk("USB console greeting scheduling failed: %d\n", ret);
	}
#else
	printk("Hello, Zephyr World (%s)\n", CONFIG_BOARD_TARGET);
#endif
}

FUNC_NORETURN void app_panic(void)
{
	printk("Rust panic\n");
	k_panic();
	CODE_UNREACHABLE;
}

extern void rust_main(void);

int main(void)
{
	rust_main();
	return 0;
}
