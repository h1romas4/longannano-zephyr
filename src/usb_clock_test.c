#include <stdint.h>

#include <gd32vf103.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#define CLOCK_WAIT_LIMIT 1000000U

BUILD_ASSERT(DT_PROP(DT_NODELABEL(cpu), clock_frequency) == 96000000);
BUILD_ASSERT(DT_PROP(DT_PATH(cpus), timebase_frequency) == 24000000);
BUILD_ASSERT(CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC == 24000000);

void __real_SystemInit(void);
void __wrap_SystemInit(void);

static void wait_for_clock(volatile uint32_t *reg, uint32_t mask, uint32_t expected)
{
	for (uint32_t i = 0; i < CLOCK_WAIT_LIMIT; i++) {
		if ((*reg & mask) == expected) {
			return;
		}
	}

	k_panic();
}

void __wrap_SystemInit(void)
{
	__real_SystemInit();
	SystemCoreClockUpdate();

	if (SystemCoreClock != 108000000U ||
	    (RCU_CFG0 & RCU_CFG0_SCSS) != RCU_SCSS_PLL ||
	    (RCU_CTL & RCU_CTL_HXTALSTB) == 0U) {
		k_panic();
	}

	RCU_CFG0 = (RCU_CFG0 & ~RCU_CFG0_SCS) | RCU_CKSYSSRC_HXTAL;
	wait_for_clock(&RCU_CFG0, RCU_CFG0_SCSS, RCU_SCSS_HXTAL);

	RCU_CTL &= ~RCU_CTL_PLLEN;
	wait_for_clock(&RCU_CTL, RCU_CTL_PLLSTB, 0U);
	RCU_CFG0 = (RCU_CFG0 & ~(RCU_CFG0_PLLMF | RCU_CFG0_PLLMF_4 |
				  RCU_CFG0_USBFSPSC)) |
		   RCU_PLL_MUL24 | RCU_CKUSB_CKPLL_DIV2;
	RCU_CTL |= RCU_CTL_PLLEN;
	wait_for_clock(&RCU_CTL, RCU_CTL_PLLSTB, RCU_CTL_PLLSTB);

	RCU_CFG0 = (RCU_CFG0 & ~RCU_CFG0_SCS) | RCU_CKSYSSRC_PLL;
	wait_for_clock(&RCU_CFG0, RCU_CFG0_SCSS, RCU_SCSS_PLL);
	SystemCoreClockUpdate();
	if (SystemCoreClock != 96000000U ||
	    (RCU_CFG0 & RCU_CFG0_USBFSPSC) != RCU_CKUSB_CKPLL_DIV2) {
		k_panic();
	}
}
