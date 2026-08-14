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
 *      fixed-radius dots on screen, and touching the "CLEAR" bar at the
 *      top wipes the canvas. Screen position is printed to Serial.
 *
 *      Consecutive touch samples during a drag can land further apart
 *      than DOT_RADIUS*2 -- confirmed on real hardware, even dragging
 *      slowly -- so drawing a dot only at each raw sample would leave
 *      small gaps between them (visible as tiny notches biting into an
 *      otherwise-solid stroke). loop() interpolates extra dots between
 *      the previous and current point whenever they're further apart
 *      than DOT_RADIUS, so the stroke stays solid regardless of the
 *      actual sampling gap.
 *
 *      (A pressure-sensitive brush -- radius scaled from XPT2046's raw
 *      Z reading -- was tried here and measured working, but Z turned
 *      out to vary by screen location as well as by force on this
 *      panel, e.g. reading noticeably lower near the top-left corner
 *      even under a firm press. That positional bias makes Z alone an
 *      unreliable stand-in for "how hard", so this sketch keeps a
 *      fixed radius instead. XPT2046::getPoint(x, y, z, ...) is still
 *      available if you want to experiment with it yourself.)
 */

#include <Arduino.h>
#include <SPI.h>
#include <ST7789.h>
#include <XPT2046.h>
#include <math.h>

ST7789 tft(D10, D7, D9);
XPT2046 touch(D4, D3);

static const uint16_t CLEAR_BAR_HEIGHT = 24;
static const uint16_t DOT_RADIUS = 2;

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
	static bool haveLast = false;
	static uint16_t lastX = 0, lastY = 0;

	uint16_t x, y;

	if (!touch.getPoint(x, y, tft.width(), tft.height())) {
		haveLast = false; // next touch-down starts a new stroke, not a line from here
		return;
	}

	Serial.print("screen=(");
	Serial.print(x);
	Serial.print(",");
	Serial.print(y);
	Serial.println(")");

	// Treat "close enough that the dot would overlap the bar" as a bar
	// touch too -- otherwise a dot centered just below the boundary
	// still bleeds DOT_RADIUS pixels up into the bar, and since the bar
	// itself is only ever drawn once at startup (never refreshed), that
	// stray sliver stays there permanently, clear or not.
	if (y < CLEAR_BAR_HEIGHT + DOT_RADIUS) {
		tft.fillRect(0, CLEAR_BAR_HEIGHT, tft.width(), tft.height() - CLEAR_BAR_HEIGHT, ST7789_BLACK);
		haveLast = false;
		return;
	}

	if (haveLast) {
		float dx = (float)x - (float)lastX;
		float dy = (float)y - (float)lastY;
		float dist = sqrt(dx * dx + dy * dy);
		uint16_t steps = (uint16_t)(dist / DOT_RADIUS);
		for (uint16_t i = 1; i < steps; i++) {
			int16_t ix = lastX + (int16_t)(dx * i / steps);
			int16_t iy = lastY + (int16_t)(dy * i / steps);
			tft.fillCircle(ix, iy, DOT_RADIUS, ST7789_WHITE);
		}
	}

	tft.fillCircle(x, y, DOT_RADIUS, ST7789_WHITE);
	lastX = x;
	lastY = y;
	haveLast = true;
}
