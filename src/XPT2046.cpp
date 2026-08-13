#include "XPT2046.h"

#define CMD_READ_X 0xD0
#define CMD_READ_Y 0x90

XPT2046::XPT2046(uint8_t csPin, uint8_t irqPin)
	: _csPin(csPin), _irqPin(irqPin)
{
}

void XPT2046::begin(uint32_t spiHz)
{
	_spiSettings = SPISettings(spiHz, MSBFIRST, SPI_MODE0);

	pinMode(_csPin, OUTPUT);
	digitalWrite(_csPin, HIGH);
	pinMode(_irqPin, INPUT_PULLUP);

	SPI.begin();
}

bool XPT2046::touched(void)
{
	return digitalRead(_irqPin) == LOW;
}

uint16_t XPT2046::readChannel(uint8_t cmd)
{
	SPI.transfer(cmd);
	uint16_t hi = SPI.transfer(0x00);
	uint16_t lo = SPI.transfer(0x00);
	return ((hi << 8) | lo) >> 3; // 12-bit result, MSB first
}

bool XPT2046::getRaw(uint16_t &x, uint16_t &y)
{
	if (!touched()) {
		return false;
	}

	SPI.beginTransaction(_spiSettings);
	digitalWrite(_csPin, LOW);

	// Throwaway read after each channel switch lets the ADC input
	// settle before the sample that's kept, reducing channel crosstalk.
	readChannel(CMD_READ_X);
	uint16_t rawX = readChannel(CMD_READ_X);
	readChannel(CMD_READ_Y);
	uint16_t rawY = readChannel(CMD_READ_Y);

	digitalWrite(_csPin, HIGH);
	SPI.endTransaction();

	if (!touched()) {
		return false; // touch lifted mid-read, discard sample
	}

	x = rawX;
	y = rawY;
	return true;
}

void XPT2046::setCalibration(uint16_t rawXMin, uint16_t rawXMax,
			      uint16_t rawYMin, uint16_t rawYMax,
			      bool swapXY, bool invertX, bool invertY)
{
	_rawXMin = rawXMin;
	_rawXMax = rawXMax;
	_rawYMin = rawYMin;
	_rawYMax = rawYMax;
	_swapXY = swapXY;
	_invertX = invertX;
	_invertY = invertY;
}

bool XPT2046::getPoint(uint16_t &x, uint16_t &y, uint16_t screenW, uint16_t screenH)
{
	uint16_t rawX, rawY;

	if (!getRaw(rawX, rawY)) {
		return false;
	}

	if (_swapXY) {
		uint16_t t = rawX;
		rawX = rawY;
		rawY = t;
	}

	long px = map((long)rawX, _rawXMin, _rawXMax, 0, screenW - 1);
	long py = map((long)rawY, _rawYMin, _rawYMax, 0, screenH - 1);

	if (_invertX) { px = (screenW - 1) - px; }
	if (_invertY) { py = (screenH - 1) - py; }

	px = constrain(px, 0, screenW - 1);
	py = constrain(py, 0, screenH - 1);

	x = (uint16_t)px;
	y = (uint16_t)py;
	return true;
}
