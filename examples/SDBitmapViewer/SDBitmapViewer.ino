/*
 * SDBitmapViewer -- Waveshare_TFT_Touch library example
 *
 * Demo for the Waveshare 2.8inch TFT Touch Shield's onboard microSD
 * slot: lists 24-bit uncompressed BMP files in the card's root
 * directory, then draws them on the LCD one at a time. Touch anywhere
 * on the screen to show the next image.
 *
 * The SD slot shares the shield's single hardware SPI bus with the
 * LCD and touch controller (separate chip-selects, each idle high
 * except while actively transferring):
 *   SD_CS -> D5
 * (LCD_CS/D10, LCD_DC/D7, LCD_BL/D9, TP_CS/D4, TP_IRQ/D3 as in the
 * TouchPaint example.)
 *
 * BMP requirements: 24-bit uncompressed (BI_RGB) -- what most image
 * editors produce by default when exporting "24-bit bitmap". Images
 * up to 320x240 are supported; larger ones are clipped to the screen.
 * Put one or more such .bmp files in the SD card's root directory.
 *
 * Uses the standard Arduino SD library (bundled with the IDE).
 */

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <ST7789.h>
#include <XPT2046.h>
#include <string.h>

static const uint8_t SD_CS = D5;
static const uint8_t MAX_BMP_FILES = 16;
static const uint8_t CHUNK_PIXELS = 32; // small on purpose: keeps RAM
                                         // usage safe on 2KB AVR boards

ST7789  tft(D10, D7, D9);
XPT2046 touch(D4, D3);

char  bmpName[MAX_BMP_FILES][13];
uint8_t bmpCount = 0;
uint8_t bmpIndex = 0;

// Avoids strcasecmp()/strcasestr() -- not reliably available across
// AVR (avr-libc) and ARM (newlib) Arduino cores without extra
// includes, unlike the plain <string.h> functions used elsewhere here.
static bool hasBmpExtension(const char *name)
{
	size_t len = strlen(name);
	if (len <= 4 || name[len - 4] != '.') {
		return false;
	}
	char e1 = name[len - 3], e2 = name[len - 2], e3 = name[len - 1];
	return (e1 == 'b' || e1 == 'B') && (e2 == 'm' || e2 == 'M') && (e3 == 'p' || e3 == 'P');
}

// This library only sees the FAT 8.3 short name, never the real long
// filename -- so macOS's per-file ".bmp"-suffixed metadata sidecars
// (written as "._whatever.bmp" when copying onto a FAT/exFAT card,
// e.g. from Finder) can't be told apart from real bitmaps by name
// alone except for one reliable tell: the leading "." of "._" always
// gets stripped by 8.3 mangling, leaving the short name starting with
// the underscore that follows it. A real file starting with "_" would
// false-positive here, but that's rare enough to accept.
static bool looksLikeAppleDoubleFile(const char *name)
{
	return name[0] == '_';
}

static void scanBmpFiles(void)
{
	File dir = SD.open("/");
	bmpCount = 0;

	while (bmpCount < MAX_BMP_FILES) {
		File entry = dir.openNextFile();
		if (!entry) {
			break;
		}
		if (!entry.isDirectory() && hasBmpExtension(entry.name()) && !looksLikeAppleDoubleFile(entry.name())) {
			strncpy(bmpName[bmpCount], entry.name(), sizeof(bmpName[bmpCount]) - 1);
			bmpName[bmpCount][sizeof(bmpName[bmpCount]) - 1] = '\0';
			bmpCount++;
		}
		entry.close();
	}
	dir.close();

	Serial.print(bmpCount);
	Serial.println(" BMP file(s) found:");
	for (uint8_t i = 0; i < bmpCount; i++) {
		Serial.print("  ");
		Serial.println(bmpName[i]);
	}
}

static bool readLE16(File &f, uint16_t &v)
{
	uint8_t b[2];
	if (f.read(b, 2) != 2) {
		return false;
	}
	v = (uint16_t)b[0] | ((uint16_t)b[1] << 8);
	return true;
}

static bool readLE32(File &f, uint32_t &v)
{
	uint8_t b[4];
	if (f.read(b, 4) != 4) {
		return false;
	}
	v = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
	return true;
}

// Re-walks the root directory via openNextFile() to (re-)open the
// index'th BMP entry (same filter as scanBmpFiles()), leaving it open
// in `out` on success. Deliberately not SD.open("/" + name): reopening
// a file that way after it was already visited once via
// openNextFile() reliably fails on this library/card combination even
// though the name is correct and unchanged (confirmed on real
// hardware) -- root cause not identified in the (quite old) bundled SD
// library's internals, but walking via openNextFile() again, the same
// way scanBmpFiles() itself successfully opens every entry, reliably
// works, so that's what this uses instead of trusting the path-based
// SD.open().
static bool openBmpByIndex(uint8_t index, File &out)
{
	File dir = SD.open("/");
	uint8_t seen = 0;

	while (true) {
		File entry = dir.openNextFile();
		if (!entry) {
			dir.close();
			return false;
		}
		if (!entry.isDirectory() && hasBmpExtension(entry.name()) && !looksLikeAppleDoubleFile(entry.name())) {
			if (seen == index) {
				out = entry;
				dir.close();
				return true;
			}
			seen++;
		}
		entry.close();
	}
}

// Draws a 24-bit uncompressed BMP file at (x0,y0), clipped to the
// screen. Each row is read from SD and pushed to the LCD in small
// chunks; every chunk opens its own short-lived LCD SPI transaction
// so an SD read (its own SPI transaction on a different CS) never
// gets interleaved with an open LCD one -- the two must not overlap
// on a shared bus.
static bool drawBmp(File &f, int16_t x0, int16_t y0)
{
	uint16_t sig;
	uint32_t fileSize, reserved, dataOffset, headerSize;
	uint32_t widthU, heightU;
	uint16_t planes, bpp;
	uint32_t compression;

	bool ok = readLE16(f, sig) && sig == 0x4D42; // 'BM'
	ok = ok && readLE32(f, fileSize);
	ok = ok && readLE32(f, reserved);
	ok = ok && readLE32(f, dataOffset);
	ok = ok && readLE32(f, headerSize);
	ok = ok && readLE32(f, widthU);
	ok = ok && readLE32(f, heightU);
	ok = ok && readLE16(f, planes);
	ok = ok && readLE16(f, bpp);
	ok = ok && readLE32(f, compression);

	if (!ok || bpp != 24 || compression != 0) {
		Serial.println("unsupported BMP (need signature BM, 24-bit, uncompressed)");
		f.close();
		return false;
	}

	int32_t width = (int32_t)widthU;
	bool flipY = (int32_t)heightU > 0; // positive height = bottom-up rows
	int32_t absHeight = flipY ? (int32_t)heightU : -(int32_t)heightU;

	int16_t drawW = (int16_t)min((int32_t)tft.width() - x0, width);
	int16_t drawH = (int16_t)min((int32_t)tft.height() - y0, absHeight);
	if (drawW <= 0 || drawH <= 0) {
		f.close();
		return false;
	}

	uint32_t rowBytes = ((uint32_t)width * 3 + 3) & ~3UL; // rows padded to 4 bytes

	uint8_t  rgb[CHUNK_PIXELS * 3];
	uint16_t px[CHUNK_PIXELS];

	for (int16_t row = 0; row < drawH; row++) {
		int32_t fileRow = flipY ? (absHeight - 1 - row) : row;
		f.seek(dataOffset + (uint32_t)fileRow * rowBytes);

		int16_t col = 0;
		while (col < drawW) {
			uint8_t n = (uint8_t)min((int16_t)CHUNK_PIXELS, (int16_t)(drawW - col));
			f.read(rgb, n * 3);

			for (uint8_t i = 0; i < n; i++) {
				uint8_t b = rgb[i * 3 + 0];
				uint8_t g = rgb[i * 3 + 1];
				uint8_t r = rgb[i * 3 + 2];
				px[i] = rgb565(r, g, b);
			}

			tft.startWrite(x0 + col, y0 + row, n, 1);
			tft.writePixels(px, n);
			tft.endWrite();

			col += n;
		}
	}

	f.close();
	return true;
}

static void showCurrentBmp(void)
{
	if (bmpCount == 0) {
		return;
	}

	tft.fillScreen(ST7789_BLACK);

	Serial.print("drawing ");
	Serial.println(bmpName[bmpIndex]);

	File f;
	if (!openBmpByIndex(bmpIndex, f) || !drawBmp(f, 0, 0)) {
		tft.fillScreen(ST7789_RED); // visible failure indicator
	}
}

void setup(void)
{
	Serial.begin(115200);

	tft.begin();
	tft.setRotation(1); // landscape, 320x240
	touch.begin();
	// See TouchPaint.ino for why swapXY+invertX (not invertY): confirmed
	// against real hardware in the Zephyr port of this same shield.
	touch.setCalibration(0, 4095, 0, 4095, /*swapXY=*/true, /*invertX=*/true, /*invertY=*/false);

	tft.fillScreen(ST7789_BLACK);

	if (!SD.begin(SD_CS)) {
		Serial.println("SD.begin() failed -- check card is inserted and formatted FAT16/FAT32");
		return;
	}

	scanBmpFiles();
	showCurrentBmp();
}

void loop(void)
{
	uint16_t x, y;

	if (bmpCount == 0 || !touch.getPoint(x, y, tft.width(), tft.height())) {
		return;
	}

	bmpIndex = (bmpIndex + 1) % bmpCount;
	showCurrentBmp();

	delay(300); // simple debounce against one touch registering as several
}
