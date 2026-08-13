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
 *   2. Enters a simple touch-paint loop: dragging a finger/stylus draws
 *      on screen, and touching the "CLEAR" bar at the top wipes it.
 *      Raw + mapped touch coordinates are also printed to Serial, which
 *      is the easiest way to work out real setCalibration() values for
 *      your specific panel.
 */

#include <Arduino.h>
#include <SPI.h>
#include <ST7789.h>
#include <XPT2046.h>

ST7789 tft(D10, D7, D9);
XPT2046 touch(D4, D3);

static const uint16_t CLEAR_BAR_HEIGHT = 24;

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

	tft.begin();
	tft.setRotation(1); // landscape, 320x240

	touch.begin();
	// Uncalibrated defaults (full 0..4095 raw range, no swap/invert).
	// Watch the "raw" values printed to Serial while touching known
	// screen corners, then replace these with your panel's real range.
	touch.setCalibration(0, 4095, 0, 4095, /*swapXY=*/true, /*invertX=*/false, /*invertY=*/false);

	drawCornerTestPattern();
	delay(3000);

	tft.fillScreen(ST7789_BLACK);
	drawClearBar();
}

void loop(void)
{
	uint16_t rawX, rawY;
	uint16_t x, y;

	if (!touch.getRaw(rawX, rawY)) {
		return;
	}

	touch.getPoint(x, y, tft.width(), tft.height());

	Serial.print("raw=(");
	Serial.print(rawX);
	Serial.print(",");
	Serial.print(rawY);
	Serial.print(")  screen=(");
	Serial.print(x);
	Serial.print(",");
	Serial.print(y);
	Serial.println(")");

	if (y < CLEAR_BAR_HEIGHT) {
		tft.fillRect(0, CLEAR_BAR_HEIGHT, tft.width(), tft.height() - CLEAR_BAR_HEIGHT, ST7789_BLACK);
		return;
	}

	tft.fillCircle(x, y, 2, ST7789_WHITE);
}
