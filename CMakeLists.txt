cmake_minimum_required(VERSION 3.28.0)

option(LONGAN_NANO_USBFS_INFO "Build the USBFS register diagnostic application" OFF)
option(LONGAN_NANO_USB_CLOCK_TEST "Test a 96 MHz PLL and 48 MHz USB clock" OFF)

if(LONGAN_NANO_USB_CLOCK_TEST)
  if(NOT LONGAN_NANO_USBFS_INFO)
    message(FATAL_ERROR "The USB clock test requires LONGAN_NANO_USBFS_INFO")
  endif()
  list(APPEND EXTRA_DTC_OVERLAY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/boards/longan_nano_usb_clock.overlay")
endif()

find_package(Zephyr REQUIRED HINTS "${CMAKE_CURRENT_SOURCE_DIR}/zephyr" $ENV{ZEPHYR_BASE})
project(longannano)

if(LONGAN_NANO_USBFS_INFO)
  if(NOT CONFIG_SOC_GD32VF103)
    message(FATAL_ERROR "The USBFS diagnostic is only supported on GD32VF103")
  endif()
  target_sources(app PRIVATE src/usbfs_info.c)
  if(LONGAN_NANO_USB_CLOCK_TEST)
    target_sources(app PRIVATE src/usb_clock_test.c)
    target_compile_definitions(app PRIVATE LONGAN_NANO_USB_CLOCK_TEST)
    target_link_options(app PUBLIC "LINKER:--wrap=SystemInit")
  endif()
else()
  target_sources(app PRIVATE src/main.c)

  find_program(CARGO_EXECUTABLE cargo REQUIRED)
  if(CONFIG_SOC_GD32VF103)
    set(RUST_TARGET riscv32i-unknown-none-elf)
  elseif(CONFIG_ARCH_POSIX AND CONFIG_64BIT AND CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(x86_64|aarch64)$")
    set(RUST_TARGET "${CMAKE_HOST_SYSTEM_PROCESSOR}-unknown-linux-gnu")
  else()
    message(FATAL_ERROR "Rust target is not configured for this board")
  endif()

  set(RUST_TARGET_DIR "${CMAKE_CURRENT_BINARY_DIR}/cargo")
  set(RUST_LIBRARY "${RUST_TARGET_DIR}/${RUST_TARGET}/release/librustapp.a")
  add_custom_target(rust_app_build
    COMMAND ${CARGO_EXECUTABLE} build --locked --release --target ${RUST_TARGET}
            --target-dir ${RUST_TARGET_DIR}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    BYPRODUCTS ${RUST_LIBRARY}
    USES_TERMINAL
  )
  add_dependencies(app rust_app_build)
  target_link_libraries(app PRIVATE ${RUST_LIBRARY})
endif()
