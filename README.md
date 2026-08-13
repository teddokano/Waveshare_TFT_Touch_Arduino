# Waveshare_TFT_Touch_Arduino
Arduino driver for the [Waveshare 2.8inch TFT Touch Shield](https://www.waveshare.com/2.8inch-tft-touch-shield.htm) (Rev2.1: ST7789V LCD + XPT2046 resistive touch), Arduino Uno R3 shield form factor.

## What is this?
An Arduino library providing:
- **`ST7789`** -- graphics primitives (fill/pixel/line/rect/circle) for the shield's 240x320 ST7789V LCD
- **`XPT2046`** -- raw and calibrated touch position reading for the shield's XPT2046 touch controller

Both share the shield's single hardware SPI bus (separate chip-selects), and pin mapping is fixed by the shield's own PCB -- there's nothing to wire or configure.

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
  touch.setCalibration(0, 4095, 0, 4095, true, false, false);

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

This library targets the standard Arduino API (`pinMode`/`digitalWrite`/`SPI`) and has no board-specific dependencies. It was developed and tested against [mcx-arduino-core](https://github.com/teddokano/mcx-arduino-core) (NXP FRDM-MCX boards) but should build against any Arduino-compatible core.

## What's inside?

### Examples

Sketch|Feature
---|---
`TouchPaint`|Draws the red/green/blue/grey corner test pattern (checks LCD orientation and RGB order at a glance), then a simple touch-paint loop that also prints raw + mapped touch coordinates to Serial

After installing the library: `File` -> `Examples` -> `Waveshare_TFT_Touch` -> `TouchPaint`

## Pin mapping

Fixed by the shield's own PCB (Arduino Uno R3 header) -- not user-configurable.

Signal|Pin
---|---
LCD_CS|D10
LCD_DC|D7
LCD_BL|D9
TP_CS|D4
TP_IRQ|D3
SCLK / MOSI / MISO|D13 / D11 / D12 (hardware SPI, shared by LCD and touch)

There is no LCD reset pin exposed on the header; `ST7789::begin()` initializes the panel directly from its power-on-reset state.

## Touch calibration

`XPT2046::setCalibration()` controls how raw 12-bit ADC counts map to screen pixels. The defaults (`0..4095` on both axes, no swap/invert) are **uncalibrated** -- run the `TouchPaint` example, watch the `raw=(...)` values printed to Serial while touching known points on your panel, and adjust `setCalibration()` accordingly.

## License

MIT -- see [LICENSE](LICENSE).
