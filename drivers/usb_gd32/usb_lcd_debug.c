#include "usb_lcd_debug.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <usb_dwc2_hw.h>

static const uint8_t hex_font[16][7] = {
	{14, 17, 19, 21, 25, 17, 14}, {4, 12, 4, 4, 4, 4, 14},
	{14, 17, 1, 2, 4, 8, 31}, {30, 1, 1, 14, 1, 1, 30},
	{2, 6, 10, 18, 31, 2, 2}, {31, 16, 16, 30, 1, 1, 30},
	{14, 16, 16, 30, 17, 17, 14}, {31, 1, 2, 4, 8, 8, 8},
	{14, 17, 17, 14, 17, 17, 14}, {14, 17, 17, 15, 1, 2, 12},
	{14, 17, 17, 31, 17, 17, 17}, {30, 17, 17, 30, 17, 17, 30},
	{14, 17, 16, 16, 16, 17, 14}, {28, 18, 17, 17, 17, 18, 28},
	{31, 16, 16, 30, 16, 16, 31}, {31, 16, 16, 30, 16, 16, 16},
};

static const struct device *const lcd = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static const struct gpio_dt_spec heartbeat = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static atomic_t irq_counts[4];
static atomic_t reporting;
static atomic_t stuck_status;
static atomic_t stuck_detected;

void gd32_usb_debug_irq(uint32_t status)
{
	atomic_inc(&irq_counts[0]);
	if (status & USB_DWC2_GINTSTS_USBRST) {
		atomic_inc(&irq_counts[1]);
	}
	if (status & USB_DWC2_GINTSTS_ENUMDONE) {
		atomic_inc(&irq_counts[2]);
	}
	if (status & USB_DWC2_GINTSTS_RXFLVL) {
		atomic_inc(&irq_counts[3]);
	}
}

void gd32_usb_debug_irq_stuck(uint32_t status)
{
	atomic_set(&stuck_status, status);
	atomic_set(&stuck_detected, 1);
	atomic_set(&reporting, 1);
}

void gd32_usb_debug_start(void)
{
	atomic_set(&reporting, 1);
}

void gd32_usb_debug(uint16_t stage, int error)
{
	const uint32_t status = ((uint32_t)stage << 16) | ((uint16_t)error);
	const struct display_buffer_descriptor desc = {
		.buf_size = 48U * sizeof(uint16_t),
		.width = 48,
		.height = 1,
		.pitch = 48,
	};
	uint16_t row[48];

	printk("USB debug: %04x %d\n", stage, error);
	if (!device_is_ready(lcd)) {
		printk("USB debug LCD is not ready\n");
		return;
	}

	for (unsigned int y = 0; y < 7; y++) {
		for (unsigned int x = 0; x < 48; x++) {
			unsigned int digit = x / 6U;
			unsigned int column = x % 6U;
			unsigned int value = (status >> (28U - 4U * digit)) & 0xfU;

			row[x] = column < 5U && (hex_font[value][y] & BIT(4U - column)) ?
				 UINT16_MAX : 0U;
		}
		int ret = display_write(lcd, 56, 35 + y, &desc, row);

		if (ret < 0) {
			printk("USB debug LCD write failed: %d\n", ret);
			return;
		}
	}
}

static int gd32_usb_debug_init(void)
{
	int ret;

	if (!device_is_ready(lcd)) {
		printk("USB debug LCD is not ready\n");
		return -ENODEV;
	}
	if (!gpio_is_ready_dt(&heartbeat)) {
		printk("USB debug heartbeat GPIO is not ready\n");
		return -ENODEV;
	}
	ret = gpio_pin_configure_dt(&heartbeat, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		printk("USB debug heartbeat setup failed: %d\n", ret);
		return ret;
	}
	ret = display_blanking_off(lcd);
	if (ret < 0) {
		printk("USB debug LCD unblank failed: %d\n", ret);
		return ret;
	}
	gd32_usb_debug(0xb000, 0);
	return 0;
}

SYS_INIT(gd32_usb_debug_init, APPLICATION, 99);

static void gd32_usb_debug_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	while (true) {
		int ret;

		k_sleep(K_MSEC(500));
		ret = gpio_pin_toggle_dt(&heartbeat);
		if (ret < 0) {
			printk("USB debug heartbeat failed: %d\n", ret);
			return;
		}
		if (atomic_get(&reporting)) {
			uint16_t stage = ((atomic_get(&irq_counts[0]) & 0xffU) << 8) |
					 (atomic_get(&irq_counts[1]) & 0xffU);
			uint16_t counts = ((atomic_get(&irq_counts[2]) & 0xffU) << 8) |
					  (atomic_get(&irq_counts[3]) & 0xffU);

			if (atomic_get(&stuck_detected)) {
				uint32_t status = atomic_get(&stuck_status);

				gd32_usb_debug(status >> 16, status & 0xffffU);
			} else {
				gd32_usb_debug(stage, counts);
			}
		}
	}
}

K_THREAD_DEFINE(gd32_usb_debug_thread_id, 1024, gd32_usb_debug_thread,
		NULL, NULL, NULL, 10, 0, 0);
