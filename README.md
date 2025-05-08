## Lilygo Amoled Series Micropython firmware with Graphic Support (not LVGL)
----------------------------------------------------------------------------

It's dedicated to

- Lilygo T4-S3 AMOLED
- Lilygo T-Display S3 AMOLED
- Lilygo 1.43-inch SH8601 AMOLED
- Lilygo 1.75-inch SH8601 AMOLED
- Waveshare ESP32-S3 1.8inch AMOLED Touch

This Micropython driver is created on behalf of [nspsck](https://github.com/nspsck/RM67162_Micropython_QSPI) RM67162 driver.
It is also convergent with [russhugues](https://github.com/russhughes/st7789_mpy) ST7789 driver.
I also would like to thanks [lewisxhe](https://github.com/Xinyuan-LilyGO/LilyGo-AMOLED-Series). Your advices helped me a lot.

My main goal was to adapt a driver library that would give the same functions thant ST7789 driver, in order to
be able to get my micropythons projects working whether on PICO + ST7789 or ESP32 + RM690B0 or ESP32 + RM67162

The driver involves a frame buffer of 600x450, requiring 540ko of available ram (T4-S3 version).
The driver involves a frame buffer of 536x240, requiring 280ko of available ram (TDisplay S3 version).
In a more general way, requirements are WIDTH x HEIGHT x 2 bytes or ram.

Latest firmware is build with
- Micropython 1.25
- ESP IDF toolchain 5.3


Contents:

- [Lilygo Amoled Series Micropython firmware with Graphic Support](#Lilygo)
- [Introduction](#introduction)
- [Features](#features)
- [Documentation](#documentation) 
- [How to build](#build)
- [Optional Scripts](#optional-scripts)



## Introduction
This is the successor of the previous [lcd_binding_micropython](https://github.com/nspsck/lcd_binding_micropython). 
It is reconstructed to be more straightforward to develop on, and this allows me to test the changes before committing.

This driver is based on [esp_lcd](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/lcd.html).

Available functions: `fill, fill_rect, rect, fill_circle, circle, pixel, vline, hline, colorRGB, bitmap, brightness, line, text, text_len, write, write_len` etc.
For full details please visit [the documentation](#documentation).

Fixed spacing font was created by russhughes.
Variable spacing font was created by myself

The firmware is provided each time when I update this repo. 

## Working : 
- text         monosized font
- text_len
- write        variable spacing font
- write_len
- draw         hershey vectorial font
- draw_len
- fill
- rect
- fill_rect
- trian
- fill_tria
- bubble_rect
- fill_bubble_rect
- circle
- fill_circle
- polygons
- fill_polygons
- fast_hline
- fast_vline
- line
- jpg         display JPG image
- brightness
- rotation
- and so on...

## to check : 
- As far as I know everything is working as expected

## To-DO List : 
- add ttf support
- optimize triangle function

## Features

The following display driver ICs are supported:
- Support for RM690B0 displays

Supported boards：
- [LILYGO T4 S3 AMOLED](https://www.lilygo.cc/products/t4-s3)
- [LILYGO T-DISPLAY S3 AMOLED](https://www.lilygo.cc/products/t-display-s3-amoled)
- [WAVESHARE ESP32-S3 1.8 AMOLED](https://www.waveshare.com/esp32-s3-touch-amoled-1.8.htm)

| Driver IC | Display IC |    SPI    |   QSPI    |   I8080   |   DPI     |
| --------- | ---------- | --------- | --------- | --------- | --------- |
| ESP32-S3  |  RM690B0   |    NO     |   YES     |    NO     |    NO     |
| ESP32-S3  |  RM67162   |    NO     |   YES     |    NO     |    NO     |
| ESP32-S3  |  SH8601    |    NO     |   YES     |    NO     |    NO     |


## Documentation
In general, the screen starts at 0 and goes to 599 x 449 for T4-S3 (resp 535 x 239 for T-Display S3), that's a total resolution of 600 x 450 (resp 536 x 240).
All drawing functions should be called with this in mind.


- `amoled.COLOR`

  This returns a predefined color that can be directly used for drawing. Available options are: BLACK, BLUE, RED, GREEN, CYAN, MAGENTA, YELLOW, WHITE

- `init()`

  Must be called to initialize the display.

- `deinit()`

  Deinit the tft object and release the memory used for the framebuffer.

- `reset()`

  Soft reset the display.

- `rotation(value)`

  Rotate the display, value range: 0 - 3.

- `brightness(value)`

  Set the screen brightness, value range: 0 - 100, in percentage.

- `disp_off()`

  Turn off the display.

- `disp_on()`

  Turn on the display.

- `backlight_on()`

  Turn on the backlight, this is equal to `brightness(100)`.

- `backlight_off()`

  Turn off the backlight, this is equal to `brightness(0)`.

- `refresh()`

  Force refresh of the whole screen. 
  Use full when i it parameter auto_refresh=false has been used

- `invert_color()`

  Invert the display color.

- `height()`

  Returns the height of the display.

- `width()`

  Returns the width of the display.

- `colorRGB(r, g, b)`

  Call this function to get the rgb color for the drawing.

- `pixel(x, y, color)`

  Draw a single pixel at the position (x, y) with color.

- `hline(x, y, l, color)`

  Draw a horizontal line starting at the position (x, y) with color and length l. 

- `vline(x, y, l, color)`

  Draw a vertical line starting at the position (x, y) with color and length l.

  - `line(x0, y0, x1, y1, color)`

  Draw a line (not anti-aliased) from (x0, y0) to (x1, y1) with color.

- `fill(color)`

  Fill the entire screen with the color.

- `fill_rect(x, y, w, h, color)`

  Draw a rectangle starting from (x, y) with the width w and height h and fill it with the color.

- `rect(x, y, w, h, color)`

  Draw a rectangle starting from (x, y) with the width w and height h of the color.

  - `fill_trian(x1, y1, x2, y2, x3, y3, color)`

  Draw a triangle starting from fill it with the color.

- `trian(x1, y1, x2, y2, x3, y3, color)`

  Draw a triangle starting of the color without filling it.

- `fill_bubble_rect(x, y, w, h, color)`

  Draw a rounded text-bubble-like rectangle starting from (x, y) with the width w and height h and fill it with the color.

- `bubble_rect(x, y, w, h, color)`

  Draw a rounded text-bubble-like rectangle starting from (x, y) with the width w and height h of the color.

- `fill_circle(x, y, r, color)`

  Draw a circle with the middle point (x, y) with the radius r and fill it with the color.

- `circle(x, y, r, color)`

  Draw a circle with the middle point (x, y) with the radius r of the color.

- `bitmap(x0, y0, x1, y1, buf)`

  Bitmap the content of a bytearray buf filled with color565 values starting from (x0, y0) to (x1, y1). Currently, the user is responsible for the provided buf content.

- `text(font, text, x, y, fg_color, bg_color)`

  Write text using bitmap fonts starting at (x, y) using foreground color `fg_color` and background color `bg_color`.

- `text_len(bitmap_font, s)`

  Returns the string's width in pixels if printed in the specified font.

- `write(bitmap_font, s, x, y[, fg, bg, background_tuple, fill_flag])`

  Write text to the display using the specified proportional or Monospace bitmap font module with the coordinates as the upper-left corner of the text. The foreground and background colors of the text can be set by the optional arguments `fg` and `bg`, otherwise the foreground color defaults to `WHITE` and the background color defaults to `BLACK`.

  The `font2bitmap` utility creates compatible 1 bit per pixel bitmap modules from Proportional or Monospaced True Type fonts. The character size, foreground, background colors, and characters in the bitmap module may be specified as parameters. Use the -h option for details. If you specify a buffer_size during the display initialization, it must be large enough to hold the widest character (HEIGHT * MAX_WIDTH * 2).

  For more information please visit: [https://github.com/nspsck/st7735s_WeAct_Studio_TFT_port/tree/main](https://github.com/nspsck/st7735s_WeAct_Studio_TFT_port/tree/main)

- `write_len(bitap_font, s)`
  Returns the string's width in pixels if printed in the specified font.

- `draw(vector_font, s, x, y[, fg, scale])`
  Draw text to the display using the specified Hershey vector font with the coordinates as the lower-left corner of the text. The foreground color of the text can be set by the optional argument fg.

- `draw_len(vector_font, s[, scale]`
  Returns the string's width in pixels if drawn with the specified font.

## Related Repositories

- [framebuf-plus](https://github.com/lbuque/framebuf-plus)


## Build
This is only for reference. Since ESP-IDF v5.0.2 to v5.4.1, the path to the cmake files changes.
The instruction below are working with ESP-IDF v5.4.1 and Micropython 1.25.
Please note that for now, compilation crashes with Micropython 1.26_preview
```Shell
cd ~
git clone https://github.com/dobodu/Lilygo-Amoled-Micropython.git

# to the micropython directory
cd micropython/port/esp32
make  BOARD=ESP32_GENERIC_S3 BOARD_VARIANT=FLASH_16M_SPIRAM_OCT USER_C_MODULES=~/Lilygo-Amoled-Micropython CFLAGS_EXTRA=-DMODULE_AMOLED_ENABLED=1
```
You may also want to modify the `sdkconfig` before building in case to get the 16MB storage.
```Shell
cd micropython/ports/esp32
# use the editor you prefer
vim boards/ESP32_GENERIC_S3/sdkconfig.board 
```
Change it to:
```Shell
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_ESPTOOLPY_FLASHFREQ_80M=y
CONFIG_ESPTOOLPY_AFTER_NORESET=y

CONFIG_ESPTOOLPY_FLASHSIZE_4MB=
CONFIG_ESPTOOLPY_FLASHSIZE_8MB=
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions-16MiB.csv"
```
If the esp_lcd related functions are missing, do the following:
```Shell
cd micropython/port/esp32
# use the editor you prefer
vim esp32_common.cmake
```
Jump to section `APPEND IDF_COMPONENTS` and add `esp_lcd` to the list should fix this.


# Note: 
Scrolling does not work. Maybe using a framebuffer (provided by Micropython) to scroll will work.

