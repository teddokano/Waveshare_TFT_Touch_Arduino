/*
 * TouchPaint -- Waveshare_TFT_Touch library example
 *
 * Demo for the Waveshare 2.8inch TFT Touch Shield (Rev2.1: ST7789V +
 * XPT2046). https://www.waveshare.com/2.8inch-tft-touch-shield.htm
 *
 * Shield pins (fixed on the PCB, Arduino Uno R3 header):
 *   LCD_CS  -> D10   LCD_DC -> D7   LCD_BL -> D9
 *   TP_CS   -> D4    TP_IRQ -> D3
 *   SCLK/MOSI/MISO -> D13/D11/D12 (hardware SPI, shared by LCD + touch)
 *
 * `D10`/`D7`/... below are normally only defined by some 32-bit cores
 * (this shield's mcx-arduino-core, UNO R4 Minima's renesas_uno) -- the
 * classic AVR core real UNO R3 boards use doesn't define them at all.
 * <ST7789.h>/<XPT2046.h> pull in src/PinCompat.h, which fills in
 * D0..D13 on AVR cores only, so this stays portable everywhere while
 * still reading as D10 instead of a bare, less obvious 10.
 *
 * What this sketch does:
 *   1. Draws the classic red/green/blue/grey corner test pattern so the
 *      LCD's orientation and RGB byte order can be checked at a glance.
 *   2. Enters a pressure-sensitive touch-paint loop: dragging a finger/
 *      stylus draws a dot whose radius grows with how hard you press
 *      (XPT2046's raw Z pressure reading), and touching the "CLEAR" bar
 *      at the top wipes the canvas. Screen position, raw pressure, and
 *      the radius it maps to are printed to Serial.
 *
 *      MIN_Z/MAX_Z below are set from real light/firm touches measured
 *      on actual hardware (z~1000 light, z~1500 firm -- pressure range
 *      is quite narrow in practice, much tighter than the driver's
 *      z-threshold-to-max theoretical span) -- retune them to taste, or
 *      by watching the printed "z=" values on your own panel.
 */

#include <Arduino.h>
#include <SPI.h>
#include <ST7789.h>
#include <XPT2046.h>

ST7789 tft(D10, D7, D9);
XPT2046 touch(D4, D3);

static const uint16_t CLEAR_BAR_HEIGHT = 24;

// Pressure-to-radius mapping; see the header comment for where these
// numbers come from.
static const uint16_t MIN_Z = 1000;  // light touch
static const uint16_t MAX_Z = 1500;  // firm press
static const uint16_t MIN_RADIUS = 1;
static const uint16_t MAX_RADIUS = 8;

static void drawCornerTestPattern(void)
{
	uint16_t w = tft.width();
	uint16_t h = tft.height();
	uint16_t rw = w * 40 / 100;
	uint16_t rh = h * 40 / 100;

	tft.fillScreen(ST7789_BLACK);
	tft.fillRect(0, 0, rw, rh, ST7789_RED);                 // top-left
	tft.fillRect(w - rw, 0, rw, rh, ST7789_GREEN);           // top-right
	tft.fillRect(w - rw, h - rh, rw, rh, ST7789_BLUE);        // bottom-right
	tft.fillRect(0, h - rh, rw, rh, ST7789_GREY);             // bottom-left
}

static void drawClearBar(void)
{
	tft.fillRect(0, 0, tft.width(), CLEAR_BAR_HEIGHT, ST7789_RED);
}

void setup(void)
{
	Serial.begin(115200);
	while (!Serial)
		;

	tft.begin();
	tft.setRotation(1); // landscape, 320x240
	touch.begin();
	// Full 0..4095 raw range (uncalibrated), but swapXY/invertX/invertY
	// are confirmed correct for this rotation on real hardware -- see
	// the Zephyr port of this same shield
	// (~/dev/Zephyr/waveshare_2_8_lcd/boards/shields/waveshare_2_8_tft_touch/waveshare_2_8_tft_touch.overlay),
	// whose overlay comment logs the same finding: the raw X channel
	// tracks the panel's vertical axis correctly (top=low, bottom=high),
	// while the raw Y channel tracks the horizontal axis but reversed
	// (left=high, right=low) -- hence swapXY with only invertX set.
	// Run the TouchCalibration example if your panel needs a tighter
	// raw range than the full 0..4095 span.
	touch.setCalibration(0, 4095, 0, 4095, /*swapXY=*/true, /*invertX=*/true, /*invertY=*/false);

	drawCornerTestPattern();
	delay(3000);

	tft.fillScreen(ST7789_BLACK);
	drawClearBar();
}

void loop(void)
{
	uint16_t x, y, z;

	if (!touch.getPoint(x, y, z, tft.width(), tft.height())) {
		return;
	}

	uint16_t radius = constrain(map(z, MIN_Z, MAX_Z, MIN_RADIUS, MAX_RADIUS), MIN_RADIUS, MAX_RADIUS);

	Serial.print("screen=(");
	Serial.print(x);
	Serial.print(",");
	Serial.print(y);
	Serial.print(")  z=");
	Serial.print(z);
	Serial.print("  r=");
	Serial.println(radius);

	// Treat "close enough that the dot could overlap the bar" as a bar
	// touch too -- otherwise a dot centered just below the boundary
	// still bleeds up into the bar, and since the bar itself is only
	// ever drawn once at startup (never refreshed), that stray sliver
	// stays there permanently, clear or not. Sized against MAX_RADIUS
	// (not the current dot's radius) so the boundary never has to move.
	if (y < CLEAR_BAR_HEIGHT + MAX_RADIUS) {
		tft.fillRect(0, CLEAR_BAR_HEIGHT, tft.width(), tft.height() - CLEAR_BAR_HEIGHT, ST7789_BLACK);
		return;
	}

	tft.fillCircle(x, y, radius, ST7789_WHITE);
}
