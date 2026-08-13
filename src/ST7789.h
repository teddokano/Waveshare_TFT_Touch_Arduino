/*
 * Minimal ST7789V driver for the Waveshare 2.8inch TFT Touch Shield
 * (Rev2.1), written against the standard Arduino API (pinMode/
 * digitalWrite/SPI) so it builds with any Arduino-compatible core --
 * developed against mcx-arduino-core (NXP FRDM-MCX boards).
 *
 * Panel pins are fixed by the shield's own PCB (Arduino Uno R3 header),
 * not user-configurable:
 *   LCD_CS -> D10   LCD_DC -> D7   LCD_BL -> D9
 *   SCLK/MOSI/MISO -> D13/D11/D12 (hardware SPI)
 * There is no LCD reset pin exposed on the header.
 *
 * SPI mode is mode 0 (CPOL=0/CPHA=0).
 *
 * Provenance of the init sequence in ST7789.cpp:
 *  - Register command set and init flow (which registers to touch, in
 *    what order, one command + parameter block at a time) are modeled on
 *    the Zephyr project's ST7789V driver, which is clearly licensed:
 *      zephyr/drivers/display/{display_st7789v.c,.h}
 *      Copyright (c) 2017 Jan Van Winkel <jan.van_winkel@dxplore.eu>
 *      Copyright (c) 2019 Nordic Semiconductor ASA
 *      Copyright (c) 2019 Marc Reilly
 *      Copyright (c) 2019 PHYTEC Messtechnik GmbH
 *      Copyright (c) 2020 Endian Technologies AB
 *      Copyright (c) 2022 Basalte bv
 *      SPDX-FileCopyrightText: 2026 Abderrahmane JARMOUNI
 *      SPDX-License-Identifier: Apache-2.0
 *      https://github.com/zephyrproject-rtos/zephyr/tree/main/drivers/display
 *  - The panel-specific tuning values sent through that flow (vcom,
 *    gctrl, vrhs, vdvs, porch/pwctrl1/gamma parameter bytes) are
 *    Waveshare's own published values for this exact panel, confirmed
 *    against their STM32 HAL reference for this shield
 *    (STM32/ShowImage/Drivers/LCD/lcd_driver.c, lcd_init()) -- not a
 *    generic/borrowed ST7789 init table.
 */

#ifndef ST7789_H
#define ST7789_H

#include <Arduino.h>
#include <SPI.h>
#include "PinCompat.h" // D0..D13 pin-name aliases on cores that lack them

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
	return ((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3);
}

#define ST7789_BLACK   0x0000
#define ST7789_WHITE   0xFFFF
#define ST7789_RED     0xF800
#define ST7789_GREEN   0x07E0
#define ST7789_BLUE    0x001F
#define ST7789_YELLOW  0xFFE0
#define ST7789_CYAN    0x07FF
#define ST7789_MAGENTA 0xF81F
#define ST7789_GREY    0x8410

class ST7789 {
public:
	ST7789(uint8_t csPin, uint8_t dcPin, uint8_t blPin);

	void begin(uint32_t spiHz = 24000000UL);
	void setRotation(uint8_t rotation); // 0..3, 90 degree steps
	void setBacklight(bool on);

	uint16_t width(void) const  { return _width; }
	uint16_t height(void) const { return _height; }

	void fillScreen(uint16_t color);
	void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
	void drawPixel(int16_t x, int16_t y, uint16_t color);
	void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color);
	void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color);
	void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
	void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);
	void drawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color);
	void fillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color);

	// Streaming pixel writes, for callers pushing image data (e.g. a
	// bitmap decoded from SD) instead of a single flat color. Each
	// startWrite()/writePixels()/endWrite() sequence must push exactly
	// w*h pixels, in row-major order, between startWrite() and
	// endWrite() -- and must not be interleaved with SPI activity from
	// another device sharing the bus (such as an SD card read) before
	// endWrite() closes the transaction.
	void startWrite(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
	void writePixels(const uint16_t *colors, uint32_t count);
	void endWrite(void);

private:
	void setAddrWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
	void writeCommand(uint8_t cmd);
	void writeData(uint8_t data);
	void writeData16(uint16_t data);
	void pushColor(uint16_t color, uint32_t count);
	// One command byte followed by its parameter bytes, e.g.
	// Zephyr's st7789v_transmit(dev, cmd, data, len).
	void transmit(uint8_t cmd, const uint8_t *data, size_t len);

	uint8_t _csPin, _dcPin, _blPin;
	uint8_t _rotation;
	uint16_t _width, _height;
	SPISettings _spiSettings;
};

#endif // !ST7789_H
