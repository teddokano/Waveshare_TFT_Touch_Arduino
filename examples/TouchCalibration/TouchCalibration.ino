/*
 * TouchCalibration -- Waveshare_TFT_Touch library example
 *
 * Guided touch calibration wizard: shows a crosshair near each of the
 * four screen corners in turn, waits for a touch on it, and from the
 * four raw readings works out real setCalibration() arguments for
 * your specific panel -- printed to Serial ready to paste into your
 * own sketch. TouchPaint's raw=(...) printout requires you to eyeball
 * that math yourself; this does it for you, including auto-detecting
 * axis swap and inversion (unlike TouchPaint's hardcoded guesses).
 *
 * After calibrating, touching the screen draws a dot so you can
 * confirm the result tracks your finger correctly before copying the
 * printed setCalibration() line into your own sketch. Nothing here is
 * persisted (e.g. to EEPROM) -- rerun this sketch whenever you need
 * new values.
 */

#include <Arduino.h>
#include <SPI.h>
#include <ST7789.h>
#include <XPT2046.h>

ST7789  tft(D10, D7, D9);
XPT2046 touch(D4, D3);

static const int16_t TARGET_MARGIN = 30; // inset from the physical edge

static void drawCrosshair(int16_t x, int16_t y)
{
	const int16_t s = 10;
	tft.drawFastHLine(x - s, y, 2 * s + 1, ST7789_YELLOW);
	tft.drawFastVLine(x, y - s, 2 * s + 1, ST7789_YELLOW);
	tft.drawCircle(x, y, s / 2, ST7789_YELLOW);
}

// Shows a crosshair at (sx,sy) and blocks until it's touched, then
// waits for release so one physical touch isn't captured twice.
static void sampleAtPoint(const char *label, int16_t sx, int16_t sy, uint16_t &rawA, uint16_t &rawB)
{
	tft.fillScreen(ST7789_BLACK);
	drawCrosshair(sx, sy);

	Serial.print("Touch the ");
	Serial.print(label);
	Serial.println(" crosshair...");

	uint16_t a, b;
	while (!touch.getRaw(a, b)) {
	}
	rawA = a;
	rawB = b;

	Serial.print("  raw=(");
	Serial.print(a);
	Serial.print(",");
	Serial.print(b);
	Serial.println(")");

	while (touch.touched()) {
	}
	delay(200); // settle before the next crosshair appears
}

// From the raw readings at the two TARGET_MARGIN-inset touch points on
// one axis, works out (min, max, invert) for setCalibration() -- the
// raw value setCalibration() needs is at the true edges (screen
// position 0 and axisSize-1), not at the inset points actually
// touched, so this extrapolates the two measured points out to those
// edges by their measured slope rather than using them directly.
// Using the inset readings as-is (i.e. as if position 0 and
// axisSize-1 had been touched) was tried first and confirmed on real
// hardware to systematically undersize the raw span, since it's
// really calibrating against only (axisSize-1-2*TARGET_MARGIN) pixels
// of physical travel -- the visible symptom was the tracked point
// overshooting real finger movement, worst near the screen edges.
// min/max always come out numerically ordered, with invert recording
// whether the raw axis actually runs backwards.
static void computeAxisCalib(float lowEnd, float highEnd, uint16_t axisSize, uint16_t &outMin, uint16_t &outMax, bool &outInvert)
{
	float span = (float)(axisSize - 1 - 2 * TARGET_MARGIN); // physical pixels between the two touch points
	float slope = (highEnd - lowEnd) / span;                // raw counts per physical pixel
	float rawAt0   = lowEnd - slope * TARGET_MARGIN;
	float rawAtMax = highEnd + slope * TARGET_MARGIN;

	if (rawAt0 <= rawAtMax) {
		outMin = (uint16_t)constrain(rawAt0, 0.0f, 4095.0f);
		outMax = (uint16_t)constrain(rawAtMax, 0.0f, 4095.0f);
		outInvert = false;
	} else {
		outMin = (uint16_t)constrain(rawAtMax, 0.0f, 4095.0f);
		outMax = (uint16_t)constrain(rawAt0, 0.0f, 4095.0f);
		outInvert = true;
	}
}

void setup(void)
{
	Serial.begin(115200);
	while (!Serial) {
	}

	tft.begin();
	tft.setRotation(1); // landscape, 320x240 -- match your own sketch's rotation
	touch.begin();

	uint16_t w = tft.width();
	uint16_t h = tft.height();

	uint16_t rawA_TL, rawB_TL, rawA_TR, rawB_TR;
	uint16_t rawA_BL, rawB_BL, rawA_BR, rawB_BR;

	sampleAtPoint("top-left", TARGET_MARGIN, TARGET_MARGIN, rawA_TL, rawB_TL);
	sampleAtPoint("top-right", w - TARGET_MARGIN, TARGET_MARGIN, rawA_TR, rawB_TR);
	sampleAtPoint("bottom-left", TARGET_MARGIN, h - TARGET_MARGIN, rawA_BL, rawB_BL);
	sampleAtPoint("bottom-right", w - TARGET_MARGIN, h - TARGET_MARGIN, rawA_BR, rawB_BR);

	// getRaw()'s first value ("channel A") is the XPT2046's own X ADC
	// channel, unrelated to which screen axis it happens to track on
	// this panel -- decide that from how much channel A moves along
	// screen X vs along screen Y across the four corners.
	long spreadA_alongScreenX = (long)(rawA_TR + rawA_BR) - (long)(rawA_TL + rawA_BL);
	long spreadA_alongScreenY = (long)(rawA_BL + rawA_BR) - (long)(rawA_TL + rawA_TR);
	bool swapXY = abs(spreadA_alongScreenY) > abs(spreadA_alongScreenX);

	uint16_t chX_TL, chX_TR, chX_BL, chX_BR;
	uint16_t chY_TL, chY_TR, chY_BL, chY_BR;
	if (!swapXY) {
		chX_TL = rawA_TL; chX_TR = rawA_TR; chX_BL = rawA_BL; chX_BR = rawA_BR;
		chY_TL = rawB_TL; chY_TR = rawB_TR; chY_BL = rawB_BL; chY_BR = rawB_BR;
	} else {
		chX_TL = rawB_TL; chX_TR = rawB_TR; chX_BL = rawB_BL; chX_BR = rawB_BR;
		chY_TL = rawA_TL; chY_TR = rawA_TR; chY_BL = rawA_BL; chY_BR = rawA_BR;
	}

	uint16_t rawXMin, rawXMax, rawYMin, rawYMax;
	bool invertX, invertY;
	computeAxisCalib((chX_TL + chX_BL) / 2.0f, (chX_TR + chX_BR) / 2.0f, w, rawXMin, rawXMax, invertX);
	computeAxisCalib((chY_TL + chY_TR) / 2.0f, (chY_BL + chY_BR) / 2.0f, h, rawYMin, rawYMax, invertY);

	touch.setCalibration(rawXMin, rawXMax, rawYMin, rawYMax, swapXY, invertX, invertY);

	Serial.println();
	Serial.println("Calibration complete. Paste this into your sketch:");
	Serial.print("  touch.setCalibration(");
	Serial.print(rawXMin);
	Serial.print(", ");
	Serial.print(rawXMax);
	Serial.print(", ");
	Serial.print(rawYMin);
	Serial.print(", ");
	Serial.print(rawYMax);
	Serial.print(", ");
	Serial.print(swapXY ? "true" : "false");
	Serial.print(", ");
	Serial.print(invertX ? "true" : "false");
	Serial.print(", ");
	Serial.print(invertY ? "true" : "false");
	Serial.println(");");
	Serial.println();
	Serial.println("Touch the screen now to verify -- a dot should track your finger.");

	tft.fillScreen(ST7789_BLACK);
}

void loop(void)
{
	uint16_t x, y;
	if (touch.getPoint(x, y, tft.width(), tft.height())) {
		tft.fillCircle(x, y, 3, ST7789_GREEN);
	}
}
