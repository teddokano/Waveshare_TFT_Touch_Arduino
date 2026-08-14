# Waveshare_TFT_Touch_Arduino
[![Compile examples](https://github.com/teddokano/Waveshare_TFT_Touch_Arduino/actions/workflows/compile-examples.yml/badge.svg)](https://github.com/teddokano/Waveshare_TFT_Touch_Arduino/actions/workflows/compile-examples.yml)

Arduino driver for the [Waveshare 2.8inch TFT Touch Shield](https://www.waveshare.com/2.8inch-tft-touch-shield.htm) (Rev2.1: ST7789V LCD + XPT2046 resistive touch), Arduino Uno R3 shield form factor.

## What is this?
An Arduino library providing:
- **`ST7789`** -- graphics primitives (fill/pixel/line/rect/circle) plus `startWrite()`/`writePixels()`/`endWrite()` for streaming arbitrary pixel data (e.g. a decoded image) to the shield's 240x320 ST7789V LCD
- **`XPT2046`** -- raw and calibrated touch position reading for the shield's XPT2046 touch controller

Both share the shield's single hardware SPI bus with the onboard microSD slot (separate chip-selects), and pin mapping is fixed by the shield's own PCB -- there's nothing to wire or configure. When mixing in SD card access (see `SDBitmapViewer`), keep each device's `SPI.beginTransaction()`/`endTransaction()` pair short and non-overlapping -- don't hold an `ST7789::startWrite()` transaction open across an SD read.

```cpp
#include <SPI.h>
#include <ST7789.h>
#include <XPT2046.h>

ST7789  tft(D10, D7, D9);   // LCD_CS, LCD_DC, LCD_BL
XPT2046 touch(D4, D3);      // TP_CS, TP_IRQ

void setup() {
  tft.begin();
  tft.setRotation(1);       // landscape, 320x240
  touch.begin();
  touch.setCalibration(0, 4095, 0, 4095, true, true, false);

  tft.fillScreen(ST7789_BLACK);
  tft.fillCircle(160, 120, 40, ST7789_RED);
}

void loop() {
  uint16_t x, y;
  if (touch.getPoint(x, y, tft.width(), tft.height())) {
    tft.fillCircle(x, y, 2, ST7789_WHITE);
  }
}
```

## Supported device
Type#|Header file|Interface|Notes
---|---|---|---
[ST7789V](https://www.waveshare.com/2.8inch-tft-touch-shield.htm)|`ST7789.h`|SPI (mode 0, up to 24MHz)|240x320 LCD controller
[XPT2046](https://www.waveshare.com/2.8inch-tft-touch-shield.htm)|`XPT2046.h`|SPI (mode 0, up to ~2MHz)|4-wire resistive touch controller, shares the LCD's SPI bus

## Getting started

Copy (or `git clone`) this repository into your Arduino `libraries/` folder, then restart the Arduino IDE.

This library targets the standard Arduino API (`pinMode`/`digitalWrite`/`SPI`) and has no board-specific dependencies.

> **Note:** `D10`/`D7`/... pin names are normally only defined by some 32-bit cores (this shield's mcx-arduino-core, UNO R4 Minima's renesas_uno) -- the classic AVR core real UNO R3 boards use doesn't define them at all. `<ST7789.h>`/`<XPT2046.h>` pull in [`src/PinCompat.h`](src/PinCompat.h), which fills in `D0`..`D13` on AVR cores only (guarded on the `__AVR__` compiler macro, not `#ifndef D0`, since D0 is an enum member rather than a preprocessor macro on cores that already define it), so `D10`-style pin names work everywhere.

### Build status

Every example is compiled against all three boards below on each push via [`.github/workflows/compile-examples.yml`](.github/workflows/compile-examples.yml) (see the badge at the top of this file) -- this only proves the examples *compile*, not that they've been run on real hardware.

Board|Core|Compiles (CI)|Run on real hardware
---|---|---|---
FRDM-MCXA153|[mcx-arduino-core](https://github.com/teddokano/mcx-arduino-core)|Yes, except `SDBitmapViewer`\*|`TouchPaint` only
UNO R4 Minima|`arduino:renesas_uno`|Yes|`TouchPaint` only
UNO R3|`arduino:avr`|Yes|`TouchPaint` (LCD + touch both confirmed working)

\* `SDBitmapViewer` doesn't currently compile against mcx-arduino-core: the standard `SD` library's low-level SdFat backend references bare `MOSI`/`MISO`/`SCK` pin macros that mcx-arduino-core doesn't define (it only provides prefixed equivalents like `SPI_MOSI`). That's a gap in the core itself, not in this library.

## What's inside?

### Examples

Sketch|Feature
---|---
`TouchPaint`|Draws the red/green/blue/grey corner test pattern (checks LCD orientation and RGB order at a glance), then a simple touch-paint loop that also prints raw + mapped touch coordinates to Serial
`SDBitmapViewer`|Lists 24-bit uncompressed BMP files on the shield's onboard microSD slot and draws them on the LCD; touch anywhere to show the next image. Needs the standard `SD` library (bundled with the Arduino IDE)
`GraphicsPrimitivesDemo`|Exercises every drawing primitive (fillRect/drawRect/lines/circles) and cycles through all four `setRotation()` orientations every few seconds -- a quick bring-up/visual-regression check, LCD only
`TouchCalibration`|Guided wizard: touch four on-screen crosshairs and it prints a ready-to-paste `setCalibration()` call, auto-detecting axis swap/inversion instead of TouchPaint's hardcoded guesses

After installing the library: `File` -> `Examples` -> `Waveshare_TFT_Touch` -> pick a sketch

## Pin mapping

Fixed by the shield's own PCB (Arduino Uno R3 header) -- not user-configurable.

Signal|Pin
---|---
LCD_CS|D10
LCD_DC|D7
LCD_BL|D9
TP_CS|D4
TP_IRQ|D3
SD_CS|D5 (onboard microSD slot; see `SDBitmapViewer`)
SCLK / MOSI / MISO|D13 / D11 / D12 (hardware SPI, shared by LCD, touch, and SD)

There is no LCD reset pin exposed on the header; `ST7789::begin()` initializes the panel directly from its power-on-reset state.

## Touch calibration

`XPT2046::setCalibration()` controls how raw 12-bit ADC counts map to screen pixels. The examples' defaults (`0..4095` raw range on both axes, `swapXY=true, invertX=true, invertY=false`) use the full uncalibrated ADC range, but the swap/invert flags themselves are confirmed correct for this rotation against real hardware -- see the companion [Zephyr port of this same shield](https://github.com/teddokano/zephyr-waveshare-2.8-tft-touch-shield), whose overlay comment logs the same finding from touching all four screen corners: the raw X channel tracks the panel's vertical axis correctly (top=low, bottom=high), while the raw Y channel tracks the horizontal axis but reversed (left=high, right=low).

If your panel's raw range doesn't span close to the full `0..4095`, run the `TouchCalibration` example -- it prints a ready-to-paste `setCalibration()` call tuned to your specific unit. (`TouchPaint`'s `raw=(...)` Serial printout also works for calibrating by hand, if you'd rather.)

## Troubleshooting

**Nothing on the SPI bus responds (LCD stays dark, touch never registers) no matter how correct the wiring/software looks:** on at least some shield units, the solder-bridge jumpers **SB1/SB2/SB3** on the back of the PCB (near R34/R35/R36 on the schematic) ship unbridged, leaving part of the SPI bus physically open. Bridge them (solder blob or 0-ohm resistor) before debugging anything in software. This is a real hardware fault found the hard way while bringing this same shield up under Zephyr -- it isn't specific to Arduino, so it can bite this library's examples too.

**On FRDM-MCXA153, LCD_CS (D10) writes report success but the panel never responds:** the same Zephyr bring-up found that this board's *default* SPI pin mux routes D10 to the LPSPI peripheral's own hardware chip-select function rather than plain GPIO -- with that mux in place, `digitalWrite()` on D10 has no effect on the physical pin at all, even though every SPI transfer and GPIO call reports success in software (see `frdm_mcxa153.overlay` in that project for the fix, on the Zephyr side). This library hasn't hit that failure mode on mcx-arduino-core, but if LCD_CS ever seems to have "no effect" there, check whether D10 is muxed to hardware SPI CS instead of plain GPIO in the board's pin configuration.

**LCD works, but touch is dead or wildly intermittent (comes and goes across power cycles) even with SB1/SB2/SB3 bridged:** before suspecting software, check the physical header contact for TP_IRQ/TP_CS -- on real UNO R3 hardware this turned out to be exactly that: a marginal pin/socket contact (dust in the header socket, a header pin not making firm contact) that behaved differently run to run, including working fine on some boots and not on others. Cleaning the socket and slightly bending the pin for a firmer contact fixed it outright, with zero code changes. Since the LCD is write-only and never exercises TP_IRQ, MISO, or TP_CS at all, it can look completely healthy while touch's marginal connection fails intermittently -- if touch behaves inconsistently across reboots/reseats, suspect the physical connection before the driver.

## Acknowledgements

This library targets the exact hardware of the Waveshare 2.8inch TFT Touch Shield, but its driver *logic* -- the ST7789V register init flow and the XPT2046 SPI read protocol -- is modeled on the [Zephyr Project](https://www.zephyrproject.org/)'s own clearly-licensed (Apache-2.0) drivers for these chips, not on Waveshare's unlicensed vendor sample code:

- `src/ST7789.cpp` init sequence structure: [`zephyr/drivers/display/display_st7789v.c`](https://github.com/zephyrproject-rtos/zephyr/blob/main/drivers/display/display_st7789v.c) -- Copyright (c) 2017 Jan Van Winkel, 2019 Nordic Semiconductor ASA, 2019 Marc Reilly, 2019 PHYTEC Messtechnik GmbH, 2020 Endian Technologies AB, 2022 Basalte bv, 2026 Abderrahmane JARMOUNI. SPDX-License-Identifier: Apache-2.0.
- `src/XPT2046.cpp` read protocol: [`zephyr/drivers/input/input_xpt2046.c`](https://github.com/zephyrproject-rtos/zephyr/blob/main/drivers/input/input_xpt2046.c) -- Copyright (c) 2023 Seppo Takalo. SPDX-License-Identifier: Apache-2.0.

The panel-specific tuning values sent through that flow (gamma/VCOM/porch parameter bytes) are Waveshare's own published values for this exact panel, confirmed against their STM32 HAL reference code for this shield -- registers-and-values of this kind are treated as the panel's factual configuration data rather than as copyrightable expression, same as every other ST7789V driver for this panel converges on very similar numbers.

The default `swapXY`/`invertX`/`invertY` touch calibration flags in the examples, and the SB1/SB2/SB3 and FRDM-MCXA153 CS-mux notes above, come from real-hardware findings logged in the same author's [Zephyr shield support for this hardware](https://github.com/teddokano/zephyr-waveshare-2.8-tft-touch-shield) (touch corner-logging results and debugging notes in `waveshare_2_8_tft_touch.overlay` and `frdm_mcxa153.overlay`) -- these are hardware facts rather than Zephyr-specific code, but are credited here since that's where they were actually discovered.

## License

MIT -- see [LICENSE](LICENSE). Portions of `src/ST7789.cpp` and `src/XPT2046.cpp` are structured after Apache-2.0-licensed Zephyr Project code; see Acknowledgements above.
