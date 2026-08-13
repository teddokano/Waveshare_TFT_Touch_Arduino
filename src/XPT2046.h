/*
 * Minimal XPT2046 resistive touch controller driver for the Waveshare
 * 2.8inch TFT Touch Shield (Rev2.1), sharing the LCD's hardware SPI bus
 * with its own chip-select.
 *
 * Panel pins fixed by the shield PCB:
 *   TP_CS -> D4   TP_IRQ -> D3
 *
 * Read protocol (channel bit encoding, the single interleaved 9-byte
 * SPI transfer that reads Z1/Z2/X/Y in one shot, and gating a "touched"
 * result on pressure Z exceeding a threshold rather than trusting IRQ
 * alone) is modeled on the Zephyr project's XPT2046 driver, which is
 * clearly licensed:
 *   zephyr/drivers/input/input_xpt2046.c
 *   Copyright (c) 2023 Seppo Takalo
 *   SPDX-License-Identifier: Apache-2.0
 *   https://github.com/zephyrproject-rtos/zephyr/blob/main/drivers/input/input_xpt2046.c
 * That driver in turn cites the XPT2046 datasheet
 * (https://www.waveshare.com/w/upload/9/98/XPT2046-EN.pdf) as the
 * source for the channel/timing details -- the control-byte encoding
 * itself (START | CHANNEL | MODE | SER/DFR | PD) is the chip's own
 * documented protocol, not anyone's original expression.
 *
 * Unlike Zephyr's interrupt-driven design, this driver is polled from
 * loop() -- TP_IRQ is used only as a cheap pre-check to skip the SPI
 * transfer when the panel clearly isn't touched; the pressure (Z)
 * check remains the authoritative "is this a real touch" gate.
 *
 * Calibration (raw ADC range, axis swap/invert) is uncalibrated by
 * default -- tune setCalibration() against real touches on the panel,
 * same caveat as the shield's Zephyr devicetree overlay.
 */

#ifndef XPT2046_H
#define XPT2046_H

#include <Arduino.h>
#include <SPI.h>
#include "PinCompat.h" // D0..D13 pin-name aliases on cores that lack them

class XPT2046 {
public:
	XPT2046(uint8_t csPin, uint8_t irqPin);

	void begin(uint32_t spiHz = 1000000UL);

	// True while the panel is being touched (reads TP_IRQ, active low;
	// a cheap pre-check only -- getRaw()/getPoint() also confirm via
	// pressure before reporting a touch).
	bool touched(void);

	// Raw 12-bit ADC touch position (0..4095 on each axis). Returns
	// false if not currently touched.
	bool getRaw(uint16_t &x, uint16_t &y);

	// Same as getRaw(), also reporting the raw Z pressure reading used
	// internally to confirm a touch (see setPressureThreshold()).
	bool getRaw(uint16_t &x, uint16_t &y, uint16_t &z);

	// Minimum Z pressure reading for a sample to count as a real touch.
	// Default (100) matches Zephyr's xpt2046 z-threshold default.
	void setPressureThreshold(uint16_t threshold) { _zThreshold = threshold; }

	// Configure how getPoint() maps raw ADC counts to screen pixels.
	void setCalibration(uint16_t rawXMin, uint16_t rawXMax,
			     uint16_t rawYMin, uint16_t rawYMax,
			     bool swapXY, bool invertX, bool invertY);

	// Touch position mapped to screen pixel coordinates
	// (0..screenW-1, 0..screenH-1) using the current calibration.
	bool getPoint(uint16_t &x, uint16_t &y, uint16_t screenW, uint16_t screenH);

private:
	// One interleaved SPI transfer reading Z1, Z2, X, and Y (16
	// clocks-per-conversion mode); see XPT2046.cpp for the layout.
	void readRaw9(uint16_t &x, uint16_t &y, uint16_t &z);

	uint8_t _csPin, _irqPin;
	SPISettings _spiSettings;

	uint16_t _zThreshold = 100;
	uint16_t _rawXMin = 0, _rawXMax = 4095;
	uint16_t _rawYMin = 0, _rawYMax = 4095;
	bool _swapXY = false;
	bool _invertX = false;
	bool _invertY = false;
};

#endif // !XPT2046_H
