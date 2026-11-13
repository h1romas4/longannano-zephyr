#ifndef LONGAN_USB_LCD_DEBUG_H
#define LONGAN_USB_LCD_DEBUG_H

#include <stdint.h>
#include <zephyr/sys/util.h>

#if defined(CONFIG_UDC_GD32VF103_LCD_DEBUG)
void gd32_usb_debug(uint16_t stage, int error);
void gd32_usb_debug_irq(uint32_t status);
void gd32_usb_debug_irq_stuck(uint32_t status);
void gd32_usb_debug_start(void);
#else
static inline void gd32_usb_debug(uint16_t stage, int error)
{
	ARG_UNUSED(stage);
	ARG_UNUSED(error);
}
static inline void gd32_usb_debug_irq(uint32_t status)
{
	ARG_UNUSED(status);
}
static inline void gd32_usb_debug_start(void)
{
}
static inline void gd32_usb_debug_irq_stuck(uint32_t status)
{
	ARG_UNUSED(status);
}
#endif

#endif
