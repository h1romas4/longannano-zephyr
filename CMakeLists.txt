cmake_minimum_required(VERSION 3.28.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(longannano)

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
