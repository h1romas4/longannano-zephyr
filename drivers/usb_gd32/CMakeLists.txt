if(CONFIG_UDC_GD32VF103)
  zephyr_library()
  zephyr_library_sources(udc_gd32vf103.c)
  zephyr_library_sources_ifdef(CONFIG_UDC_GD32VF103_LCD_DEBUG usb_lcd_debug.c)
  zephyr_library_include_directories(
    ${ZEPHYR_BASE}/drivers/usb/udc
    ${ZEPHYR_BASE}/drivers/usb/common
  )
  target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../src/usb_clock_test.c)
  target_link_options(app PUBLIC "LINKER:--wrap=SystemInit")
endif()
