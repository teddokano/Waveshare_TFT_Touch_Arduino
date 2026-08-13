/*
 * GraphicsPrimitivesDemo -- Waveshare_TFT_Touch library example
 *
 * Visual smoke test for every ST7789 drawing primitive (fillRect,
 * drawRect, drawFastHLine/VLine, drawLine, drawCircle, fillCircle),
 * redrawn every few seconds after stepping through all four
 * setRotation() orientations -- a quick way to confirm a panel is
 * wired correctly (and to see all four rotations) during bring-up on
 * new hardware.
 *
 * No touch or SD card needed -- LCD only.
 */

#include <Arduino.h>
#include <SPI.h>
#include <ST7789.h>

ST7789 tft(D10, D7, D9);

static void drawFrame(void)
{
	uint16_t w = tft.width();
	uint16_t h = tft.height();

	tft.fillScreen(ST7789_BLACK);

	tft.drawRect(2, 2, w - 4, h - 4, ST7789_WHITE);

	tft.fillRect(10, 10, 40, 30, ST7789_RED);
	tft.drawRect(60, 10, 40, 30, ST7789_GREEN);

	tft.drawFastHLine(10, 50, w - 20, ST7789_YELLOW);
	tft.drawFastVLine(w / 2, 55, min(h - 65, 20), ST7789_YELLOW);

	tft.drawLine(10, 70, w - 10, h - 10, ST7789_CYAN);
	tft.drawLine(10, h - 10, w - 10, 70, ST7789_MAGENTA);

	uint16_t r = min(w, h) / 5;
	tft.drawCircle(w / 2, h / 2, r, ST7789_BLUE);
	tft.fillCircle(w - r - 10, h - r - 10, r / 2, ST7789_GREY);
}

void setup(void)
{
	tft.begin();
	tft.setRotation(0);
	drawFrame();
}

void loop(void)
{
	static uint8_t rotation = 0;

	delay(3000);
	rotation = (rotation + 1) % 4;
	tft.setRotation(rotation);
	drawFrame();
}
