#![no_std]

use core::ffi::{c_char, CStr};
#[cfg(not(test))]
use core::panic::PanicInfo;

const LCD_WIDTH: usize = 160;
const LCD_HEIGHT: usize = 80;
const FONT_SCALE: usize = 2;
const FONT_WIDTH: usize = 5;
const FONT_HEIGHT: usize = 7;

const FONT: &[(u8, [u8; FONT_HEIGHT])] = &[
    (b' ', [0, 0, 0, 0, 0, 0, 0]),
    (b',', [0, 0, 0, 0, 0, 4, 8]),
    (b'H', [17, 17, 17, 31, 17, 17, 17]),
    (b'W', [17, 17, 17, 21, 21, 21, 10]),
    (b'Z', [31, 1, 2, 4, 8, 16, 31]),
    (b'd', [1, 1, 15, 17, 17, 17, 15]),
    (b'e', [0, 0, 14, 17, 31, 16, 14]),
    (b'h', [16, 16, 30, 17, 17, 17, 17]),
    (b'l', [12, 4, 4, 4, 4, 4, 14]),
    (b'o', [0, 0, 14, 17, 17, 17, 14]),
    (b'p', [0, 0, 30, 17, 30, 16, 16]),
    (b'r', [0, 0, 22, 25, 16, 16, 16]),
    (b'y', [0, 0, 17, 17, 15, 1, 14]),
];

extern "C" {
    fn app_lcd_init() -> i32;
    fn app_lcd_write_row(y: u16, pixels: *const u16) -> i32;
    fn app_lcd_show() -> i32;
    fn app_leds_init() -> i32;
    fn app_led_set(index: u32, active: bool) -> i32;
    fn app_sim_led_init() -> i32;
    fn app_sim_led_update() -> i32;
    fn app_sleep_one_second();
    fn app_report_error(operation: *const c_char, status: i32);
    fn app_report_message(message: *const c_char);
    fn app_report_greeting();
    #[cfg(not(test))]
    fn app_panic() -> !;
}

#[cfg(not(test))]
#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    unsafe { app_panic() }
}

fn check(operation: &CStr, status: i32) -> Result<(), ()> {
    if status < 0 {
        unsafe { app_report_error(operation.as_ptr(), status) };
        Err(())
    } else {
        Ok(())
    }
}

fn render_row(y: usize, row: &mut [u16; LCD_WIDTH]) -> Result<(), &'static CStr> {
    const LINES: [(&[u8], usize); 2] = [(b"Hello, Zephyr", 19), (b"World", 45)];

    row.fill(0);
    for &(text, line_y) in &LINES {
        let width = (text.len() * (FONT_WIDTH + 1) - 1) * FONT_SCALE;
        if width > LCD_WIDTH {
            return Err(c"Greeting line is too wide");
        }
        if y < line_y || y >= line_y + FONT_HEIGHT * FONT_SCALE {
            continue;
        }
        let start = (LCD_WIDTH - width) / 2;
        for (i, character) in text.iter().enumerate() {
            let Some((_, glyph)) = FONT.iter().find(|(ch, _)| ch == character) else {
                return Err(c"Unsupported greeting character");
            };
            let bits = glyph[(y - line_y) / FONT_SCALE];
            for x in 0..FONT_WIDTH * FONT_SCALE {
                if bits & (1 << (FONT_WIDTH - 1 - x / FONT_SCALE)) != 0 {
                    row[start + i * (FONT_WIDTH + 1) * FONT_SCALE + x] = u16::MAX;
                }
            }
        }
    }
    Ok(())
}

fn draw_greeting() -> Result<(), ()> {
    let mut row = [0u16; LCD_WIDTH];

    for y in 0..LCD_HEIGHT {
        if let Err(message) = render_row(y, &mut row) {
            unsafe { app_report_message(message.as_ptr()) };
            return Err(());
        }
        // The C bridge consumes this fixed-size row synchronously.
        check(c"LCD row write", unsafe {
            app_lcd_write_row(y as u16, row.as_ptr())
        })?;
    }
    check(c"LCD unblank", unsafe { app_lcd_show() })
}

fn run() -> Result<(), ()> {
    check(c"LCD initialization", unsafe { app_lcd_init() })?;
    check(c"LED initialization", unsafe { app_leds_init() })?;
    check(c"Simulated LED initialization", unsafe {
        app_sim_led_init()
    })?;
    draw_greeting()?;
    unsafe { app_report_greeting() };

    let mut active = 0u32;
    loop {
        for led in 0..3 {
            check(c"LED update", unsafe { app_led_set(led, led == active) })?;
        }
        check(c"Simulated LED update", unsafe { app_sim_led_update() })?;
        unsafe { app_sleep_one_second() };
        active = (active + 1) % 3;
    }
}

#[no_mangle]
extern "C" fn rust_main() {
    let _ = run();
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn greeting_has_expected_pixels_and_empty_margins() {
        let mut row = [123; LCD_WIDTH];
        render_row(18, &mut row).unwrap();
        assert!(row.iter().all(|pixel| *pixel == 0));

        render_row(19, &mut row).unwrap();
        assert_eq!(
            &row[3..13],
            &[u16::MAX, u16::MAX, 0, 0, 0, 0, 0, 0, u16::MAX, u16::MAX]
        );

        let first = row;
        render_row(20, &mut row).unwrap();
        assert_eq!(row, first);

        render_row(45, &mut row).unwrap();
        assert!(row.iter().any(|pixel| *pixel == u16::MAX));
        render_row(59, &mut row).unwrap();
        assert!(row.iter().all(|pixel| *pixel == 0));
    }
}
