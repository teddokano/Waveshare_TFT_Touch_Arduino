#include "XPT2046.h"

// Control-byte bit layout and channel numbers, matching Zephyr's
// Apache-2.0 xpt2046 driver (input_xpt2046.c) -- see XPT2046.h for full
// attribution. These bits are the XPT2046's own documented protocol
// (START | CHANNEL | MODE | SER/DFR | PD), not original expression.
#define XPT_START      0x80
#define XPT_CHANNEL(ch) (((ch) & 0x7) << 4)
#define XPT_POWER_ON   0x03
#define XPT_POWER_OFF  0x00

enum xpt2046_channel {
	CH_Y  = 1,
	CH_Z1 = 3,
	CH_Z2 = 4,
	CH_X  = 5,
};

// Extracts a 12-bit conversion result straddling buf[idx]/buf[idx+1],
// same bit layout as Zephyr's CONVERT_U16().
static inline uint16_t convert12(const uint8_t *buf, int idx)
{
	return ((uint16_t)(buf[idx] & 0x7f) << 5) | (buf[idx + 1] >> 3);
}

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

void XPT2046::readRaw9(uint16_t &x, uint16_t &y, uint16_t &z)
{
	// Read Z1, Z2, X, Y using 16-clocks-per-conversion mode: each
	// command byte's result is clocked out one command later, so a
	// single 9-byte full-duplex transfer captures all four channels.
	uint8_t buf[9] = {
		(uint8_t)(XPT_START | XPT_CHANNEL(CH_Z1) | XPT_POWER_ON), 0,
		(uint8_t)(XPT_START | XPT_CHANNEL(CH_Z2) | XPT_POWER_ON), 0,
		(uint8_t)(XPT_START | XPT_CHANNEL(CH_X)  | XPT_POWER_ON), 0,
		(uint8_t)(XPT_START | XPT_CHANNEL(CH_Y)  | XPT_POWER_OFF), 0,
		0,
	};

	SPI.beginTransaction(_spiSettings);
	digitalWrite(_csPin, LOW);
	SPI.transfer(buf, sizeof(buf));
	digitalWrite(_csPin, HIGH);
	SPI.endTransaction();

	uint16_t z1 = convert12(buf, 1);
	uint16_t z2 = convert12(buf, 3);
	x = convert12(buf, 5);
	y = convert12(buf, 7);
	// Pressure figure of merit; larger while pressed harder, matches
	// Zephyr's own z1 + 4096 - z2 formula.
	z = z1 + 4096 - z2;
}

bool XPT2046::getRaw(uint16_t &x, uint16_t &y, uint16_t &z)
{
	if (!touched()) {
		return false; // cheap pre-check, avoids an SPI transfer
	}

	// The instant right as a finger lifts off is electrically noisy --
	// TP_IRQ can still read low for a moment while the resistive
	// contact itself is unstable, which can produce a single garbage
	// x/y sample that still happens to clear the pressure threshold.
	// Two back-to-back reads of a real, steady touch land within a few
	// counts of each other; a release-glitch sample usually doesn't
	// match its neighbor. Reject the pair when they disagree by more
	// than COORD_TOLERANCE, same debounce strategy used by most
	// XPT2046 drivers (e.g. Waveshare's own reference code averages
	// two reads and rejects them past a fixed error range).
	const uint16_t COORD_TOLERANCE = 100;

	uint16_t x1, y1, z1;
	readRaw9(x1, y1, z1);
	if (z1 <= _zThreshold) {
		return false;
	}

	uint16_t x2, y2, z2;
	readRaw9(x2, y2, z2);
	if (z2 <= _zThreshold) {
		return false;
	}

	if (abs((int32_t)x1 - (int32_t)x2) > COORD_TOLERANCE ||
	    abs((int32_t)y1 - (int32_t)y2) > COORD_TOLERANCE) {
		return false;
	}

	x = (uint16_t)(((uint32_t)x1 + x2) / 2);
	y = (uint16_t)(((uint32_t)y1 + y2) / 2);
	z = (uint16_t)(((uint32_t)z1 + z2) / 2);
	return true;
}

bool XPT2046::getRaw(uint16_t &x, uint16_t &y)
{
	uint16_t z;
	return getRaw(x, y, z);
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

bool XPT2046::getPoint(uint16_t &x, uint16_t &y, uint16_t &z, uint16_t screenW, uint16_t screenH)
{
	uint16_t rawX, rawY;

	if (!getRaw(rawX, rawY, z)) {
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

bool XPT2046::getPoint(uint16_t &x, uint16_t &y, uint16_t screenW, uint16_t screenH)
{
	uint16_t z;
	return getPoint(x, y, z, screenW, screenH);
}
