# Longan Nano Zephyr application

A Zephyr Hello World application for the Longan Nano. It displays
"Hello, Zephyr World" on two lines on the LCD and cycles the RGB LED through
red, green, and blue at one-second intervals.

![Longan Nano displaying Hello, Zephyr World](docs/images/longannano-zephyr.jpg)

The display rendering and LED control are implemented in Rust (`no_std`).
The interfaces to Zephyr drivers and Devicetree are implemented in C.

The project root is a west workspace. Zephyr is pinned to a Zephyr 4.5.0
commit as a Git submodule. The GD32 HAL revision is specified by that version
of Zephyr's west manifest.

```text
longannano/
├── .west/              # Generated locally
├── CMakeLists.txt
├── Cargo.toml
├── Cargo.lock
├── modules/            # Fetched by west
├── prj.conf
├── src/
│   ├── lib.rs          # Display rendering and LED control
│   └── main.c          # Interface to Zephyr drivers
└── zephyr/             # Git submodule
```

## Setup

Install the [Zephyr host dependencies](https://docs.zephyrproject.org/4.5.0/develop/getting_started/index.html)
and run the commands below from this directory. Also install Rust and Cargo
using [rustup](https://rustup.rs/) and add the Rust target for Longan Nano.

```sh
git clone https://github.com/h1romas4/longannano-zephyr
cd longannano-zephyr
git submodule update --init
rustup target add riscv32i-unknown-none-elf
export ZEPHYR_BASE="$PWD/zephyr"
python3 -m venv .venv
. .venv/bin/activate
pip install west -r zephyr/scripts/requirements-base.txt
west init -l zephyr
west update --narrow --fetch-opt=--depth=1 hal_gigadevice
west sdk install -t riscv64-zephyr-elf
```

`west init` is needed only once. After updating the Zephyr submodule commit,
run `west update --narrow --fetch-opt=--depth=1 hal_gigadevice` again to obtain
the matching HAL. If additional Zephyr modules are needed, run `west update`
for those modules in the same workspace.

This project does not use Zephyr 4.5.0's official Rust module because it
generates references to disabled GPIO nodes on Longan Nano, causing the
build to fail. Instead, CMake uses Cargo to build a Rust static library
without external dependencies and links it with Zephyr. Rust accesses
devices through the C bridge.

## Building and flashing

Activate the virtual environment and build with west from the project root.

```sh
. .venv/bin/activate
export ZEPHYR_BASE="$PWD/zephyr"
west build -b longan_nano -d build/west/longan .
```

Flash from the project root:

```sh
west flash -d build/west/longan
```

By default, `west flash` uses OpenOCD with an external programmer. To use USB
DFU, put the board in DFU mode and run
`west flash -d build/west/longan --runner dfu-util` instead.

For the Lite version, specify a separate build directory from the project root:

```sh
west build -b longan_nano/gd32vf103/lite -d build/west/lite .
```

To build the USBFS register diagnostic instead of the normal application, use a
separate build directory:

```sh
west build -b longan_nano -d build/west/usbfs-info . -- \
  -DLONGAN_NANO_USBFS_INFO=ON
```

The diagnostic reads the USBFS core configuration and reset FIFO registers,
prints the values to USART0, and displays them on the LCD. Press and release
the BOOT0 button to advance to the next page. It enables the USBFS AHB clock
and resets the peripheral, but does not start USB transfers or change the
PLL/USB clock divider. See
[the USB CDC ACM investigation notes](docs/longan-nano-usb-cdc-acm.md) for what
to report from the display.

For the separate experimental 96 MHz PLL / 48 MHz USB clock diagnostic,
see the same investigation notes. It does not start USB communication.

### USB CDC ACM implementation status

An experimental project-local USB device controller supports CDC ACM on a
Longan Nano. The normal build still uses USART0. To opt in to a CDC ACM
console for the normal LCD/LED application, use a separate build directory:

```sh
west build -b longan_nano -d build/west/usb-console . -- \
  -DLONGAN_NANO_USB_CONSOLE=ON
west flash -d build/west/usb-console --runner dfu-util
```

The board must be in DFU mode before flashing. Release BOOT0 and reset after
flashing if necessary. The console redirects Zephyr's console and shell UART to
CDC ACM; it does not enable a shell or implement an echo. The application
prints its greeting once, after the first terminal asserts DTR; later terminal
connections do not repeat it. The LCD and LED functionality is unchanged.
The Lite variant also builds with `-b longan_nano/gd32vf103/lite` but has not
been tested on hardware.

The temporary test configuration uses VID `0x2fe3`, PID `0x0001` at the
user's request. These are Zephyr's sample IDs, **not IDs for applications
outside the Zephyr Project**. Before using or distributing an external
application, replace them with a VID/PID you are entitled to use. They can be
overridden at build time, for example:

```sh
west build -b longan_nano -d build/west/usb-console . -- \
  -DLONGAN_NANO_USB_CONSOLE=ON \
  -DCONFIG_CDC_ACM_SERIAL_VID=0xYOUR_VID \
  -DCONFIG_CDC_ACM_SERIAL_PID=0xYOUR_PID
```

Replace the placeholders with hexadecimal values. The IDs and descriptor strings
are configured in `boards/longan_nano_usb_console.conf`. The console build is
experimental; on Longan Nano hardware the USB device enumerated and the full
startup greeting was received after opening the port.

To build the upstream echo sample with LCD diagnostics:

```sh
west build -b longan_nano -d build/west/usb-cdc-test zephyr/samples/subsys/usb/cdc_acm -- \
  -DEXTRA_ZEPHYR_MODULES="$PWD/drivers/usb_gd32" \
  -DDTC_OVERLAY_FILE="$PWD/boards/longan_nano_usb_cdc_test.overlay" \
  -DEXTRA_CONF_FILE="$PWD/drivers/usb_gd32/lcd_debug.conf"
west flash -d build/west/usb-cdc-test --runner dfu-util
```

Put the board in DFU mode before flashing; after flashing, release BOOT0 and
reset into the application if needed. On Linux the sample enumerated as
`/dev/ttyACM0`, and binary echo and unplug/replug were tested. Port permissions
depend on the host (often `dialout`). The sample uses Zephyr's sample VID/PID;
applications outside the Zephyr Project must use an appropriate VID/PID of their
own, not Zephyr's VID.

The Lite build fits with minimal logging, but has not been tested on hardware:

```sh
west build -b longan_nano/gd32vf103/lite -d build/west/usb-cdc-test-lite \
  zephyr/samples/subsys/usb/cdc_acm -- \
  -DEXTRA_ZEPHYR_MODULES="$PWD/drivers/usb_gd32" \
  -DDTC_OVERLAY_FILE="$PWD/boards/longan_nano_usb_cdc_test.overlay" \
  -DEXTRA_CONF_FILE="$PWD/drivers/usb_gd32/lite.conf"
```

The driver is an experimental Zephyr 4.5.0 DWC2 adaptation, not general GD32
USB support. It changes the test build's PLL to 96 MHz early in boot. Active
transfer cancellation and device disable/re-enable remain unverified.
Endpoint halt/clear was checked, but not with confirmed active transfers. A
reset during repeated traffic produced a short first echo after reconnection;
three subsequent reset trials succeeded with a 300 ms wait before echo.
The short read remains unexplained. Isochronous endpoints are not advertised.
The reset FIFO readings' relationship to physical FIFO capacity remains unresolved.

See the [USB CDC ACM investigation notes](docs/longan-nano-usb-cdc-acm.md) for
the register readings, references, and investigation details.

To flash the Lite version, run `west flash -d build/west/lite`.

The default console uses USART0 (TX: PA9, RX: PA10, 115200 baud). To see its
output, connect the RX of a 3.3 V USB-to-serial adapter to the board's TX and
connect their grounds. USB DFU is separate from console output.

## Running with native_sim

![Simulated Longan Nano LCD and RGB LED](docs/images/longannano-zephyr-sim.png)

A Linux host with a C compiler, SDL2 development libraries, pkg-config, and a
graphical desktop is required. The simulator targets 64-bit x86_64 or aarch64
Linux and uses the host Rust target. Run the following from the project root:

```sh
. .venv/bin/activate
export ZEPHYR_BASE="$PWD/zephyr"
west build -b native_sim/native/64 -d build/west/native . -- \
  -DZEPHYR_TOOLCHAIN_VARIANT=host
west build -d build/west/native -t run
```

The LCD and RGB LED states appear in separate windows. The LCD is displayed
at an enlarged scale using the same 160×80-pixel resolution as the board.
The LED window visualizes the GPIO output state; it does not simulate the
electrical behavior of the physical LED or LCD controller. The simulator is
configured to run in real time, so the LED changes once per second. Press
Ctrl+C in the terminal to exit.

Run the Rust rendering test with `cargo test --locked`.

For more information, see the [Zephyr Longan Nano board documentation](https://docs.zephyrproject.org/4.5.0/boards/sipeed/longan_nano/doc/index.html).

## VS Code

- **Terminal:** New integrated terminals on Linux set `ZEPHYR_BASE` to the
  project-local Zephyr and add `.venv/bin` to `PATH`. Shell startup files can
  override these settings; if they do, set `ZEPHYR_BASE` after the shell starts,
  as shown in [Setup](#setup).
- **Tasks:** Use **Tasks: Run Build Task** to build native-sim, Longan Nano or
  Lite. **Tasks: Run Task** also offers flash tasks for both boards with
  OpenOCD or USB DFU. Tasks require `west` on `PATH` and the Zephyr environment
  to be configured in their terminal.
- **rust-analyzer:** Checks the host target with aborting panics, matching the
  Rust static library's release build. Check the board target separately with
  `cargo check --locked --release --target riscv32i-unknown-none-elf`.

## Continuous integration

GitHub Actions checks Rust formatting and tests, builds both Longan Nano
variants and the native simulator, and checks that the simulator starts
successfully using SDL's headless software renderer.
