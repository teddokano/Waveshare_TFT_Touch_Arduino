#include "ST7789.h"

// Native panel resolution (portrait, rotation 0)
#define PANEL_W 240
#define PANEL_H 320

// Command set and names follow Zephyr's Apache-2.0 st7789v driver
// (zephyr/drivers/display/display_st7789v.h) -- see ST7789.h for full
// attribution. Values themselves are the panel's documented register
// addresses (ST7789V datasheet), identical in every ST7789V driver.
#define ST7789_CMD_SLEEP_OUT  0x11
#define ST7789_CMD_INV_OFF    0x20
#define ST7789_CMD_DISP_ON    0x29
#define ST7789_CMD_CASET      0x2A
#define ST7789_CMD_RASET      0x2B
#define ST7789_CMD_RAMWR      0x2C
#define ST7789_CMD_MADCTL     0x36
#define ST7789_CMD_COLMOD     0x3A
#define ST7789_CMD_PORCTRL    0xB2
#define ST7789_CMD_GCTRL      0xB7
#define ST7789_CMD_VCOMS      0xBB
#define ST7789_CMD_LCMCTRL    0xC0
#define ST7789_CMD_VDVVRHEN   0xC2
#define ST7789_CMD_VRH        0xC3
#define ST7789_CMD_VDV        0xC4
#define ST7789_CMD_FRCTRL2    0xC6
#define ST7789_CMD_PWCTRL1    0xD0
#define ST7789_CMD_PVGAMCTRL  0xE0
#define ST7789_CMD_NVGAMCTRL  0xE1
#define ST7789_CMD_WRCACE     0x55 // Waveshare-specific extra register,
                                   // not part of Zephyr's generic flow

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

// Chunk size for buffer-based SPI.transfer(buf, count) bursts below.
// Kept safely under 128 bytes: at least one supported core's
// SPI.transfer(buf, count) copies the received data into a fixed
// 128-byte stack buffer sized to the caller's byte count, so a chunk
// any larger would overflow it. 32 pixels = 64 bytes.
static const uint32_t PIXEL_CHUNK = 32;

void ST7789::pushColor(uint16_t color, uint32_t count)
{
	uint8_t hi = (uint8_t)(color >> 8);
	uint8_t lo = (uint8_t)(color & 0xFF);
	uint8_t buf[PIXEL_CHUNK * 2];

	digitalWrite(_dcPin, HIGH);
	digitalWrite(_csPin, LOW);
	while (count > 0) {
		uint32_t n = count < PIXEL_CHUNK ? count : PIXEL_CHUNK;
		// SPI.transfer(buf, ...) is in-place full-duplex -- it
		// overwrites buf with whatever comes back on MISO, so it must
		// be refilled before every chunk, not just once outside the
		// loop.
		for (uint32_t i = 0; i < n; i++) {
			buf[i * 2]     = hi;
			buf[i * 2 + 1] = lo;
		}
		SPI.transfer(buf, n * 2);
		count -= n;
	}
	digitalWrite(_csPin, HIGH);
}

void ST7789::transmit(uint8_t cmd, const uint8_t *data, size_t len)
{
	writeCommand(cmd);
	for (size_t i = 0; i < len; i++) {
		writeData(data[i]);
	}
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

	// No hardware reset pin is available, so init starts directly from
	// the panel's power-on-reset state.
	writeCommand(ST7789_CMD_SLEEP_OUT);
	delay(120); // datasheet requires >=120ms after SLEEP_OUT

	// Register flow below follows Zephyr's st7789v_lcd_init() (see
	// ST7789.h for the Apache-2.0 attribution); parameter values are
	// Waveshare's own tuning for this panel.
	static const uint8_t porch_param[]  = {0x0c, 0x0c, 0x00, 0x33, 0x33};
	static const uint8_t pwctrl1_param[] = {0xa4, 0xa1};
	static const uint8_t pvgam_param[]  = {0xd0, 0x01, 0x08, 0x0f, 0x11, 0x2a, 0x36,
						0x55, 0x44, 0x3a, 0x0b, 0x06, 0x11, 0x20};
	static const uint8_t nvgam_param[]  = {0xd0, 0x02, 0x07, 0x0a, 0x0b, 0x18, 0x34,
						0x43, 0x4a, 0x2b, 0x1b, 0x1c, 0x22, 0x1f};
	uint8_t gctrl = 0x35, vcom = 0x28, lcm = 0x3c;
	uint8_t vdvvrhen = 0x01, vrh = 0x0b, vdv = 0x20, frctrl2 = 0x0f;
	uint8_t madctl = 0x00; // overwritten by setRotation() below
	uint8_t colmod = 0x55; // 16bpp RGB565
	uint8_t wrcace = 0xB0; // Waveshare-specific, not in Zephyr's generic flow

	transmit(ST7789_CMD_PORCTRL, porch_param, sizeof(porch_param));
	transmit(ST7789_CMD_FRCTRL2, &frctrl2, 1);
	transmit(ST7789_CMD_GCTRL, &gctrl, 1);
	transmit(ST7789_CMD_VCOMS, &vcom, 1);
	transmit(ST7789_CMD_VDVVRHEN, &vdvvrhen, 1);
	transmit(ST7789_CMD_VRH, &vrh, 1);
	transmit(ST7789_CMD_VDV, &vdv, 1);
	transmit(ST7789_CMD_PWCTRL1, pwctrl1_param, sizeof(pwctrl1_param));
	transmit(ST7789_CMD_MADCTL, &madctl, 1);
	transmit(ST7789_CMD_COLMOD, &colmod, 1);
	transmit(ST7789_CMD_LCMCTRL, &lcm, 1);
	transmit(ST7789_CMD_INV_OFF, nullptr, 0);
	transmit(ST7789_CMD_PVGAMCTRL, pvgam_param, sizeof(pvgam_param));
	transmit(ST7789_CMD_NVGAMCTRL, nvgam_param, sizeof(nvgam_param));
	transmit(ST7789_CMD_WRCACE, &wrcace, 1);

	writeCommand(ST7789_CMD_DISP_ON);

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
	writeCommand(ST7789_CMD_MADCTL);
	writeData(madctl);
	SPI.endTransaction();
}

void ST7789::setAddrWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
	uint16_t xe = x + w - 1;
	uint16_t ye = y + h - 1;

	writeCommand(ST7789_CMD_CASET);
	writeData16(x);
	writeData16(xe);

	writeCommand(ST7789_CMD_RASET);
	writeData16(y);
	writeData16(ye);

	writeCommand(ST7789_CMD_RAMWR);
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

void ST7789::startWrite(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
	SPI.beginTransaction(_spiSettings);
	setAddrWindow(x, y, w, h);
}

void ST7789::writePixels(const uint16_t *colors, uint32_t count)
{
	uint8_t buf[PIXEL_CHUNK * 2];

	digitalWrite(_dcPin, HIGH);
	digitalWrite(_csPin, LOW);

	uint32_t idx = 0;
	while (idx < count) {
		uint32_t n = (count - idx) < PIXEL_CHUNK ? (count - idx) : PIXEL_CHUNK;
		for (uint32_t i = 0; i < n; i++) {
			uint16_t c = colors[idx + i];
			buf[i * 2]     = (uint8_t)(c >> 8);
			buf[i * 2 + 1] = (uint8_t)(c & 0xFF);
		}
		SPI.transfer(buf, n * 2);
		idx += n;
	}

	digitalWrite(_csPin, HIGH);
}

void ST7789::endWrite(void)
{
	SPI.endTransaction();
}

void ST7789::fillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color)
{
	for (int16_t y = -r; y <= r; y++) {
		int16_t dx = (int16_t)sqrt((float)(r * r - y * y));
		drawFastHLine(x0 - dx, y0 + y, 2 * dx + 1, color);
	}
}
