/*
 * Minimal XPT2046 resistive touch controller driver for the Waveshare
 * 2.8inch TFT Touch Shield (Rev2.1), sharing the LCD's hardware SPI bus
 * with its own chip-select.
 *
 * Panel pins fixed by the shield PCB:
 *   TP_CS -> D4   TP_IRQ -> D3
 *
 * Control-byte / channel selection (0xD0 = X, 0x90 = Y; 12-bit,
 * differential mode) follows the standard XPT2046 convention used by
 * most Arduino touch libraries and by Zephyr's own xpt2046 driver.
 *
 * Calibration (raw ADC range, axis swap/invert) is uncalibrated by
 * default -- tune setCalibration() against real touches on the panel,
 * same caveat as the shield's Zephyr devicetree overlay.
 */

#ifndef XPT2046_H
#define XPT2046_H

#include <Arduino.h>
#include <SPI.h>

class XPT2046 {
public:
	XPT2046(uint8_t csPin, uint8_t irqPin);

	void begin(uint32_t spiHz = 1000000UL);

	// True while the panel is being touched (reads TP_IRQ, active low).
	bool touched(void);

	// Raw 12-bit ADC touch position (0..4095 on each axis). Returns
	// false if not currently touched.
	bool getRaw(uint16_t &x, uint16_t &y);

	// Configure how getPoint() maps raw ADC counts to screen pixels.
	void setCalibration(uint16_t rawXMin, uint16_t rawXMax,
			     uint16_t rawYMin, uint16_t rawYMax,
			     bool swapXY, bool invertX, bool invertY);

	// Touch position mapped to screen pixel coordinates
	// (0..screenW-1, 0..screenH-1) using the current calibration.
	bool getPoint(uint16_t &x, uint16_t &y, uint16_t screenW, uint16_t screenH);

private:
	uint16_t readChannel(uint8_t cmd);

	uint8_t _csPin, _irqPin;
	SPISettings _spiSettings;

	uint16_t _rawXMin = 0, _rawXMax = 4095;
	uint16_t _rawYMin = 0, _rawYMax = 4095;
	bool _swapXY = false;
	bool _invertX = false;
	bool _invertY = false;
};

#endif // !XPT2046_H
