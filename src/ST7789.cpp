#include "ST7789.h"

// Native panel resolution (portrait, rotation 0)
#define PANEL_W 240
#define PANEL_H 320

ST7789::ST7789(uint8_t csPin, uint8_t dcPin, uint8_t blPin)
	: _csPin(csPin), _dcPin(dcPin), _blPin(blPin),
	  _rotation(0), _width(PANEL_W), _height(PANEL_H)
{
}

void ST7789::writeCommand(uint8_t cmd)
{
	digitalWrite(_dcPin, LOW);
	digitalWrite(_csPin, LOW);
	SPI.transfer(cmd);
	digitalWrite(_csPin, HIGH);
}

void ST7789::writeData(uint8_t data)
{
	digitalWrite(_dcPin, HIGH);
	digitalWrite(_csPin, LOW);
	SPI.transfer(data);
	digitalWrite(_csPin, HIGH);
}

void ST7789::writeData16(uint16_t data)
{
	digitalWrite(_dcPin, HIGH);
	digitalWrite(_csPin, LOW);
	SPI.transfer16(data);
	digitalWrite(_csPin, HIGH);
}

void ST7789::pushColor(uint16_t color, uint32_t count)
{
	digitalWrite(_dcPin, HIGH);
	digitalWrite(_csPin, LOW);
	for (uint32_t i = 0; i < count; i++) {
		SPI.transfer16(color);
	}
	digitalWrite(_csPin, HIGH);
}

void ST7789::begin(uint32_t spiHz)
{
	_spiSettings = SPISettings(spiHz, MSBFIRST, SPI_MODE0);

	pinMode(_csPin, OUTPUT);
	pinMode(_dcPin, OUTPUT);
	pinMode(_blPin, OUTPUT);
	digitalWrite(_csPin, HIGH);
	digitalWrite(_blPin, HIGH); // backlight on, matches __LCD_BKL_SET() at reset

	SPI.begin();
	SPI.beginTransaction(_spiSettings);

	// --- ST7789V init sequence, byte-for-byte from Waveshare's STM32
	// reference lcd_init() for this shield (ST7789V branch). No hardware
	// reset pin is available, so init starts directly from the panel's
	// power-on-reset state.
	writeCommand(0x11);            // SLPOUT - sleep out
	delay(120);                    // datasheet requires >=120ms after SLPOUT

	writeCommand(0x36); writeData(0x00); // MADCTL (overwritten by setRotation() below)
	writeCommand(0x3A); writeData(0x55); // COLMOD - 16bpp RGB565

	writeCommand(0xB2);            // PORCTRL
	writeData(0x0c); writeData(0x0c); writeData(0x00); writeData(0x33); writeData(0x33);

	writeCommand(0xB7); writeData(0x35); // GCTRL
	writeCommand(0xBB); writeData(0x28); // VCOMS
	writeCommand(0xC0); writeData(0x3c); // LCMCTRL
	writeCommand(0xC2); writeData(0x01); // VDVVRHEN
	writeCommand(0xC3); writeData(0x0b); // VRHS
	writeCommand(0xC4); writeData(0x20); // VDVS
	writeCommand(0xC6); writeData(0x0f); // FRCTRL2

	writeCommand(0xD0);            // PWCTRL1
	writeData(0xa4); writeData(0xa1);

	writeCommand(0xE0);            // PVGAMCTRL
	writeData(0xd0); writeData(0x01); writeData(0x08); writeData(0x0f);
	writeData(0x11); writeData(0x2a); writeData(0x36); writeData(0x55);
	writeData(0x44); writeData(0x3a); writeData(0x0b); writeData(0x06);
	writeData(0x11); writeData(0x20);

	writeCommand(0xE1);            // NVGAMCTRL
	writeData(0xd0); writeData(0x02); writeData(0x07); writeData(0x0a);
	writeData(0x0b); writeData(0x18); writeData(0x34); writeData(0x43);
	writeData(0x4a); writeData(0x2b); writeData(0x1b); writeData(0x1c);
	writeData(0x22); writeData(0x1f);

	writeCommand(0x55); writeData(0xB0); // WRCACE

	writeCommand(0x29);            // DISPON

	SPI.endTransaction();

	setRotation(0);
	fillScreen(ST7789_BLACK);
}

void ST7789::setBacklight(bool on)
{
	digitalWrite(_blPin, on ? HIGH : LOW);
}

void ST7789::setRotation(uint8_t rotation)
{
	_rotation = rotation & 0x03;

	uint8_t madctl;
	switch (_rotation) {
	case 0: madctl = 0x00; _width = PANEL_W; _height = PANEL_H; break;
	case 1: madctl = 0x60; _width = PANEL_H; _height = PANEL_W; break;
	case 2: madctl = 0xC0; _width = PANEL_W; _height = PANEL_H; break;
	default: madctl = 0xA0; _width = PANEL_H; _height = PANEL_W; break;
	}

	SPI.beginTransaction(_spiSettings);
	writeCommand(0x36);
	writeData(madctl);
	SPI.endTransaction();
}

void ST7789::setAddrWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
	uint16_t xe = x + w - 1;
	uint16_t ye = y + h - 1;

	writeCommand(0x2A); // CASET
	writeData16(x);
	writeData16(xe);

	writeCommand(0x2B); // RASET
	writeData16(y);
	writeData16(ye);

	writeCommand(0x2C); // RAMWR
}

void ST7789::drawPixel(int16_t x, int16_t y, uint16_t color)
{
	if (x < 0 || y < 0 || x >= _width || y >= _height) {
		return;
	}
	SPI.beginTransaction(_spiSettings);
	setAddrWindow(x, y, 1, 1);
	writeData16(color);
	SPI.endTransaction();
}

void ST7789::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
	if (x >= _width || y >= _height || w <= 0 || h <= 0) {
		return;
	}
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > _width)  { w = _width  - x; }
	if (y + h > _height) { h = _height - y; }
	if (w <= 0 || h <= 0) {
		return;
	}

	SPI.beginTransaction(_spiSettings);
	setAddrWindow(x, y, w, h);
	pushColor(color, (uint32_t)w * h);
	SPI.endTransaction();
}

void ST7789::fillScreen(uint16_t color)
{
	fillRect(0, 0, _width, _height, color);
}

void ST7789::drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color)
{
	fillRect(x, y, w, 1, color);
}

void ST7789::drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color)
{
	fillRect(x, y, 1, h, color);
}

void ST7789::drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
	drawFastHLine(x, y, w, color);
	drawFastHLine(x, y + h - 1, w, color);
	drawFastVLine(x, y, h, color);
	drawFastVLine(x + w - 1, y, h, color);
}

void ST7789::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color)
{
	int16_t dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
	int16_t dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
	int16_t err = dx + dy, e2;

	for (;;) {
		drawPixel(x0, y0, color);
		if (x0 == x1 && y0 == y1) {
			break;
		}
		e2 = 2 * err;
		if (e2 >= dy) { err += dy; x0 += sx; }
		if (e2 <= dx) { err += dx; y0 += sy; }
	}
}

void ST7789::drawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color)
{
	int16_t x = -r, y = 0, err = 2 - 2 * r, e2;

	do {
		drawPixel(x0 - x, y0 + y, color);
		drawPixel(x0 + x, y0 + y, color);
		drawPixel(x0 + x, y0 - y, color);
		drawPixel(x0 - x, y0 - y, color);
		e2 = err;
		if (e2 <= y) {
			err += ++y * 2 + 1;
			if (-x == y && e2 <= x) {
				e2 = 0;
			}
		}
		if (e2 > x) {
			err += ++x * 2 + 1;
		}
	} while (x <= 0);
}

void ST7789::fillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color)
{
	for (int16_t y = -r; y <= r; y++) {
		int16_t dx = (int16_t)sqrt((float)(r * r - y * y));
		drawFastHLine(x0 - dx, y0 + y, 2 * dx + 1, color);
	}
}
