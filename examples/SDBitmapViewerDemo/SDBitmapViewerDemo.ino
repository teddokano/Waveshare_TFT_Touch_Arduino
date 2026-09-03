/*
 * SDBitmapViewerDemo -- Waveshare_TFT_Touch library example
 *
 * Demo for the Waveshare 2.8inch TFT Touch Shield's onboard microSD
 * slot: lists 24-bit uncompressed BMP files, then draws them on the
 * LCD one at a time.
 *
 * FRDM-MCXA153 and FRDM-MCXN947 only -- unlike SDBitmapViewer, this one
 * spends RAM freely to keep the transitions quick, so it will not fit an
 * UNO. On FRDM-MCXN947 the whole picture is decoded into RAM first and
 * every wipe direction is then equally cheap; FRDM-MCXA153 has too
 * little RAM for that and reads from the card as it draws.
 *
 * Gesture is recognized on release, not on press: loop() tracks the
 * coordinates where a touch started and where it was last seen before
 * lifting, and compares the two once the finger is lifted.
 *   - Move less than SWIPE_THRESHOLD px (or lift without much
 *     horizontal movement) -> counted as a tap, same as this sketch
 *     always did: shows the next image.
 *   - Move at least SWIPE_THRESHOLD px, mostly horizontally -> counted
 *     as a swipe: left shows the next image, right shows the previous
 *     one. With SWIPE_WIPES_SIDEWAYS set, a swipe also wipes sideways
 *     the way the finger went; clear it and every transition wipes
 *     vertically -- see DRAW_BOTTOM_UP. The time each draw takes is
 *     printed either way, which makes them comparable.
 *   - A touch that stays within LONG_PRESS_TOLERANCE of where it
 *     started for LONG_PRESS_MS, without waiting for release, counts
 *     as a long press: jumps back to the first image.
 *
 * The board's own SW2 and SW3 step through the same list without
 * touching the screen: SW2 goes back one image, SW3 forward one. Clicks
 * are counted rather than acted on one at a time -- a double click moves
 * two images, a triple three, and so on -- so nothing in between is ever
 * drawn. See pollButtons() for how the count is closed off.
 *
 * Left untouched long enough the sketch drops into a screen saver,
 * advancing at a fixed interval and looping. The next touch
 * restores the normal list and the image that was on screen; that touch
 * only wakes, so the tap or swipe it belongs to is discarded rather than
 * also acted on.
 *
 * Both lists come from one file, PLAYLIST_FILE ("/PLAYLIST.JSN") at the
 * card's root:
 *
 *   {
 *     "playlist":    ["/LOGO/COLOR.BMP", "/PICS/SUNSET.BMP"],
 *     "saver":       ["/PICS/MOUNTAIN.BMP"],
 *     "idle_ms":     60000,
 *     "interval_ms": 5000,
 *     "portrait":    false,
 *     "reverse":     false
 *   }
 *
 * Shown in exactly that order; paths can point into subfolders. Leave
 * out "saver" and the screen saver simply never starts. "idle_ms" is
 * how long the screen must go untouched before it starts and
 * "interval_ms" how long each picture stays up once it has; drop either
 * and the built-in IDLE_TIMEOUT_MS / SAVER_INTERVAL_MS applies.
 * "portrait" is for holding the board on its side: a touch is then read
 * by where it landed rather than which way it moved -- the top half
 * brings in the next picture wiping downwards, the bottom half the
 * previous one wiping upwards, and swipes stop being read at all. The
 * panel is still driven in landscape, so the pictures themselves are not
 * rotated. "reverse" turns every list round -- playlist, screen saver
 * and directory scan alike. A file holding
 * nothing but a bare array is still read as the playlist, so cards
 * written for the earlier one-list-per-file layout keep working.
 * (Extension is .JSN, not .JSON -- this sketch's SD library only
 * supports 8.3 filenames, so a 4-character extension can't be opened at
 * all; the content is still plain JSON.) See PLAYLIST.JSN alongside this
 * .ino for a ready-to-copy sample -- edit its paths to match your own
 * card, then copy it to the card's root.
 *
 * With no readable PLAYLIST_FILE, every .bmp in the card's root is shown
 * instead, in whatever order the FAT directory entries happen to be in
 * (usually the order they were written) -- see scanBmpFiles() below.
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
 *
 * Every image change is appended to LOG_FILE ("/VIEWER.LOG") on the same
 * card, timestamped with milliseconds since this run started, and each
 * run begins with a restart marker so the timestamps -- which start over
 * from zero every boot -- stay readable in a file that outlives them.
 *
 * Uses the standard Arduino SD library (bundled with the IDE).
 */

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <ST7789.h>
#include <XPT2046.h>
#include <string.h>

#if !defined(TARGET_A153) && !defined(TARGET_N947)
#error "SDBitmapViewerDemo targets FRDM-MCXA153 and FRDM-MCXN947 only. Use SDBitmapViewer for the portable version."
#endif

// FRDM-MCXN947 has 384KB of RAM, so the whole 320x240 picture fits as
// RGB565 (150KB) with room to spare. Holding it means the file is read
// once, sequentially -- the order a BMP is actually stored in -- and
// every pixel afterwards comes from RAM, so the wipe direction costs
// nothing. FRDM-MCXA153 has 24KB and cannot; it keeps reading straight
// from the card.
#if defined(TARGET_N947)
#define USE_FRAME_BUFFER 1
#else
#define USE_FRAME_BUFFER 0
#endif

static const uint8_t SD_CS = D5;

static const uint8_t MAX_BMP_FILES = 32;
static const uint8_t BMP_PATH_LEN = 40;
static const int16_t SCREEN_W = 320;
static const int16_t SCREEN_H = 240;
static const int16_t CHUNK_PIXELS = 64; // pixels per SD read / LCD burst on
                                         // the unbuffered path

// The frame-buffer path stages one column band in RAM, so its width is
// a memory cost rather than a bus cost and is kept separate.
static const int16_t BAND_PIXELS = 64;
static const char PLAYLIST_FILE[] = "/PLAYLIST.JSN";
static const uint16_t SWIPE_THRESHOLD = 20; // px: minimum press-to-release
                                             // travel, mostly horizontal, to
                                             // count as a swipe rather than a tap
static const uint16_t LONG_PRESS_TOLERANCE = 10; // px: how far a touch may
                                                  // drift from its start point
                                                  // and still count as "no
                                                  // change" for a long press
static const uint16_t LONG_PRESS_MS = 600; // ms a touch must stay within
                                            // LONG_PRESS_TOLERANCE to count
                                            // as a long press
// Default vertical direction. Tap, long press and the screen saver
// alternate between this and its opposite with the image index, so
// consecutive pictures wipe opposite ways. A swipe overrides it with a
// horizontal wipe instead -- see loop(). Only the order the rows or
// columns are sent changes; the image itself comes out identical, since
// the BMP's own top-down/bottom-up flag is handled separately.
static const bool DRAW_BOTTOM_UP = false;

// Whether a swipe wipes sideways in the direction the finger went. With
// this false every transition wipes vertically, swipes included, exactly
// as before the sideways wipe existed -- useful for comparing the two,
// since on FRDM-MCXA153 the sideways path has to read the file against
// its grain and is markedly the slower one.
#define SWIPE_WIPES_SIDEWAYS false

// The two user buttons the FRDM boards carry, named by mcx-arduino-core
// for both of them (SW2/SW3 sit on different physical pins per board, but
// the core hides that). Both are momentary switches to ground, so they
// read LOW while held; SW1 is the reset button and is not ours to use.
static const uint8_t SW_PREV = SW2; // one image back
static const uint8_t SW_NEXT = SW3; // one image forward
static const uint16_t BUTTON_DEBOUNCE_MS = 25; // ms an edge must settle for
// How long after a click the sketch keeps waiting for another one before
// acting. Long enough not to split a deliberate double click, short
// enough that a single click still feels immediate.
static const uint16_t MULTI_CLICK_MS = 400;

static const char LOG_FILE[]   = "/VIEWER.LOG";
static const unsigned long IDLE_TIMEOUT_MS  = 60000UL; // untouched this long -> screen saver
static const unsigned long SAVER_INTERVAL_MS = 5000UL; // screen saver advances this often

ST7789  tft(D10, D7, D9);
XPT2046 touch(D4, D3);

#if USE_FRAME_BUFFER
uint16_t frame[(uint32_t)SCREEN_W * SCREEN_H];      // the decoded picture
uint16_t band[(uint32_t)BAND_PIXELS * SCREEN_H];    // one column band, row-major
uint8_t  rowRgb[SCREEN_W * 3];                      // one raw BMP row
#endif

char  bmpName[MAX_BMP_FILES][BMP_PATH_LEN];
uint8_t bmpCount = 0;
uint8_t bmpIndex = 0;
bool usingPlaylist = false;

// Which way the picture paints in. PAINT_AUTO is only ever stored in
// pendingDir: it means "no caller asked for anything, use the default".
enum PaintDir : uint8_t {
	PAINT_TOP_DOWN,
	PAINT_BOTTOM_UP,
	PAINT_LEFT_RIGHT,
	PAINT_RIGHT_LEFT,
	PAINT_AUTO
};

PaintDir paintDir   = PAINT_TOP_DOWN; // direction for the image being shown now
PaintDir pendingDir = PAINT_AUTO;     // one-shot request from the gesture, if any
// Start at the built-in defaults; PLAYLIST_FILE overrides either one if
// it names it.
// Only the touch handling changes in portrait mode -- the panel is still
// driven in landscape, since the sketch has no way to know which way
// round the board is being held. What differs is that a touch is read by
// where it landed rather than which way it moved.
bool portraitMode = false;

// Applies to every list the sketch builds -- playlist, screen saver and
// the directory scan alike -- so the setting means the same thing
// wherever the pictures came from.
bool reverseOrder = false;

unsigned long idleTimeoutMs   = IDLE_TIMEOUT_MS;
unsigned long saverIntervalMs = SAVER_INTERVAL_MS;

bool saverActive = false;
uint8_t savedIndex = 0;             // where the normal list was, to come back to
unsigned long lastEventMs = 0;      // last touch, for the idle timeout
unsigned long lastSaverStepMs = 0;

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
	applyOrder();

	Serial.print(bmpCount);
	Serial.println(F(" BMP file(s) found (directory order):"));
	for (uint8_t i = 0; i < bmpCount; i++) {
		Serial.print(F("  "));
		Serial.println(bmpName[i]);
	}
}

// Both lists live in one file now:
//   { "playlist": ["/A.BMP", ...], "saver": ["/B.BMP", ...] }
// A file that is just a bare array is still accepted and taken as the
// playlist, so cards written for the older single-list format keep
// working untouched.
//
// Parsed straight off the card one character at a time -- no whole-file
// buffer, no library dependency. Only "\\" and "\"" escapes are
// recognized; anything else after a backslash is taken literally. Not a
// general JSON parser: it finds the member by name and reads the array
// that follows, so a *value* that happened to equal a key name would
// mislead it. Paths do not, which is enough here.
static const char KEY_PLAYLIST[] = "playlist";
static const char KEY_SAVER[]    = "saver";
static const char KEY_IDLE[]     = "idle_ms";
static const char KEY_PORTRAIT[] = "portrait";
static const char KEY_REVERSE[]  = "reverse";
static const char KEY_INTERVAL[] = "interval_ms";

// Reads the next double-quoted string into buf. False at end of file.
static bool nextString(File &f, char *buf, uint8_t cap)
{
	bool inString = false;
	uint8_t len = 0;

	while (f.available()) {
		char c = f.read();

		if (!inString) {
			if (c == '"') {
				inString = true;
				len = 0;
			}
			continue;
		}

		if (c == '\\' && f.available()) {
			c = f.read();
		} else if (c == '"') {
			buf[len] = '\0';
			return true;
		}

		if (len < cap - 1) {
			buf[len++] = c;
		}
	}
	return false;
}

// Fills bmpName[] from the array whose opening bracket has just been
// read, stopping at the matching close.
static void collectArray(File &f)
{
	char buf[BMP_PATH_LEN];
	bool inString = false;
	uint8_t len = 0;

	bmpCount = 0;

	while (f.available() && bmpCount < MAX_BMP_FILES) {
		char c = f.read();

		if (!inString) {
			if (c == '"') {
				inString = true;
				len = 0;
			} else if (c == ']') {
				break;
			}
			continue;
		}

		if (c == '\\' && f.available()) {
			c = f.read();
		} else if (c == '"') {
			inString = false;
			buf[len] = '\0';
			if (len > 0) {
				strncpy(bmpName[bmpCount], buf, BMP_PATH_LEN - 1);
				bmpName[bmpCount][BMP_PATH_LEN - 1] = '\0';
				bmpCount++;
			}
			continue;
		}

		if (len < BMP_PATH_LEN - 1) {
			buf[len++] = c;
		}
	}
}

// Reads the value sitting at the current position: only whitespace and
// the colon may come before it, so a member whose value is the wrong
// shape is rejected rather than having something picked out of whatever
// follows it.
static bool readNumberValue(File &f, unsigned long *out)
{
	unsigned long v = 0;
	bool anyDigit = false;

	while (f.available()) {
		char c = f.read();

		if (c >= '0' && c <= '9') {
			v = v * 10UL + (unsigned long)(c - '0');
			anyDigit = true;
			continue;
		}
		if (anyDigit) {
			break;
		}
		if (c == ':' || c == ' ' || c == '\t' || c == '\r' || c == '\n') {
			continue;
		}
		break;
	}

	if (!anyDigit) {
		return false;
	}
	*out = v;
	return true;
}

static bool readFlagValue(File &f, bool *out)
{
	while (f.available()) {
		char c = f.read();

		if (c == ':' || c == ' ' || c == '\t' || c == '\r' || c == '\n') {
			continue;
		}
		if (c == 't' || c == 'T' || c == 'f' || c == 'F') {
			*out = (c == 't' || c == 'T');
			return true;
		}
		break;
	}
	return false;
}

// All three scalar settings in a single pass over a single open file.
// Reopening PLAYLIST_FILE once per member looked tidier but did not
// survive contact with the bundled SD library, which is unreliable about
// handing back the same path repeatedly -- the fourth open came back
// empty and the last setting read silently kept its default. Walking the
// file once is both more robust and less work.
// Called wherever a list has just been filled, so "reverse" applies to
// the screen saver and the directory scan as well as the playlist.
static void applyOrder(void)
{
	if (!reverseOrder || bmpCount < 2) {
		return;
	}

	for (uint8_t i = 0, j = (uint8_t)(bmpCount - 1); i < j; i++, j--) {
		char tmp[BMP_PATH_LEN];
		memcpy(tmp, bmpName[i], BMP_PATH_LEN);
		memcpy(bmpName[i], bmpName[j], BMP_PATH_LEN);
		memcpy(bmpName[j], tmp, BMP_PATH_LEN);
	}
}

static void loadSettings(void)
{
	File f = SD.open(PLAYLIST_FILE);
	if (!f) {
		return;
	}

	char name[16];
	while (nextString(f, name, sizeof(name))) {
		if (strcmp(name, KEY_IDLE) == 0) {
			readNumberValue(f, &idleTimeoutMs);
		} else if (strcmp(name, KEY_INTERVAL) == 0) {
			readNumberValue(f, &saverIntervalMs);
		} else if (strcmp(name, KEY_PORTRAIT) == 0) {
			readFlagValue(f, &portraitMode);
		} else if (strcmp(name, KEY_REVERSE) == 0) {
			readFlagValue(f, &reverseOrder);
		}
	}
	f.close();
}

static bool loadList(const char *key)
{
	File f = SD.open(PLAYLIST_FILE);
	if (!f) {
		return false;
	}

	// First meaningful character decides the format.
	bool bareArray = false;
	while (f.available()) {
		char c = f.read();
		if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
			continue;
		}
		bareArray = (c == '[');
		break;
	}

	if (bareArray) {
		// Old format: one list, and it is the playlist.
		if (strcmp(key, KEY_PLAYLIST) != 0) {
			f.close();
			return false;
		}
		collectArray(f);
	} else {
		char name[16];
		bool found = false;

		while (nextString(f, name, sizeof(name))) {
			if (strcmp(name, key) == 0) {
				found = true;
				break;
			}
		}
		if (!found) {
			f.close();
			return false;
		}

		bool opened = false;
		while (f.available()) {
			char c = f.read();
			if (c == '[') {
				opened = true;
				break;
			}
			if (c == '"') {
				break;  // the member's value is not an array
			}
		}
		if (!opened) {
			f.close();
			return false;
		}

		collectArray(f);
	}

	f.close();

	if (bmpCount == 0) {
		return false;
	}
	applyOrder();

	Serial.print(bmpCount);
	Serial.print(F(" BMP file(s) from \""));
	Serial.print(key);
	Serial.print(F("\" in "));
	Serial.print(PLAYLIST_FILE);
	Serial.println(F(":"));
	for (uint8_t i = 0; i < bmpCount; i++) {
		Serial.print(F("  "));
		Serial.println(bmpName[i]);
	}
	return true;
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
// SD.open(). Only used for the directory-scan path -- playlist mode
// opens files straight from their JSON-given path instead (see
// showCurrentBmp()), since those names were never touched by
// openNextFile() in the first place and so don't hit this bug.
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
		Serial.println(F("unsupported BMP (need signature BM, 24-bit, uncompressed)"));
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

#if USE_FRAME_BUFFER
	// One sequential pass over the file, in the order the BMP stores it,
	// straight into RAM. Every read is contiguous, so the SD library's
	// block cache is used the way it expects.
	for (int16_t row = 0; row < drawH; row++) {
		int32_t fileRow = flipY ? (absHeight - 1 - row) : row;
		f.seek(dataOffset + (uint32_t)fileRow * rowBytes);
		f.read(rowRgb, (size_t)drawW * 3);

		uint16_t *dst = &frame[(uint32_t)row * drawW];
		for (int16_t i = 0; i < drawW; i++) {
			dst[i] = rgb565(rowRgb[i * 3 + 2], rowRgb[i * 3 + 1], rowRgb[i * 3 + 0]);
		}
	}
	f.close();

	// Now that the picture is in RAM the direction is just an ordering
	// choice, and each pass can hand the panel one big address window
	// instead of thousands of one-row ones.
	if (paintDir == PAINT_TOP_DOWN || paintDir == PAINT_BOTTOM_UP) {
		for (int16_t i = 0; i < drawH; i++) {
			int16_t row = (paintDir == PAINT_BOTTOM_UP) ? (int16_t)(drawH - 1 - i) : i;
			tft.startWrite(x0, y0 + row, drawW, 1);
			tft.writePixels(&frame[(uint32_t)row * drawW], drawW);
			tft.endWrite();
		}
	} else {
		for (int16_t i = 0; i < drawW; i += BAND_PIXELS) {
			int16_t n   = min((int16_t)BAND_PIXELS, (int16_t)(drawW - i));
			int16_t col = (paintDir == PAINT_RIGHT_LEFT) ? (int16_t)(drawW - i - n) : i;

			// Gather the band row-major, which is the order the panel
			// wants inside one address window.
			uint16_t *b = band;
			for (int16_t row = 0; row < drawH; row++) {
				const uint16_t *src = &frame[(uint32_t)row * drawW + col];
				for (int16_t k = 0; k < n; k++) {
					*b++ = src[k];
				}
			}

			tft.startWrite(x0 + col, y0, n, drawH);
			tft.writePixels(band, (uint32_t)n * drawH);
			tft.endWrite();
		}
	}

	return true;
#else
	uint8_t  rgb[CHUNK_PIXELS * 3];
	uint16_t px[CHUNK_PIXELS];

	if (paintDir == PAINT_TOP_DOWN || paintDir == PAINT_BOTTOM_UP) {
		// Row-wise, the way the file is laid out: one seek per row, and
		// every byte read is used before moving on.
		for (int16_t i = 0; i < drawH; i++) {
			int16_t row = (paintDir == PAINT_BOTTOM_UP) ? (int16_t)(drawH - 1 - i) : i;
			int32_t fileRow = flipY ? (absHeight - 1 - row) : row;
			f.seek(dataOffset + (uint32_t)fileRow * rowBytes);

			int16_t col = 0;
			while (col < drawW) {
				int16_t n = min((int16_t)CHUNK_PIXELS, (int16_t)(drawW - col));
				f.read(rgb, n * 3);

				for (int16_t k = 0; k < n; k++) {
					px[k] = rgb565(rgb[k * 3 + 2], rgb[k * 3 + 1], rgb[k * 3 + 0]);
				}

				tft.startWrite(x0 + col, y0 + row, n, 1);
				tft.writePixels(px, n);
				tft.endWrite();

				col += n;
			}
		}
	} else {
		// Column-wise, against the grain of the file: a BMP stores whole
		// rows contiguously, so a vertical band needs its own seek in
		// every single row -- drawH seeks per band instead of one, with
		// consecutive reads landing far enough apart that the SD
		// library's single block cache rarely helps. This is the slow
		// path the frame buffer above exists to avoid.
		for (int16_t i = 0; i < drawW; i += CHUNK_PIXELS) {
			int16_t n   = min((int16_t)CHUNK_PIXELS, (int16_t)(drawW - i));
			int16_t col = (paintDir == PAINT_RIGHT_LEFT) ? (int16_t)(drawW - i - n) : i;

			for (int16_t row = 0; row < drawH; row++) {
				int32_t fileRow = flipY ? (absHeight - 1 - row) : row;
				f.seek(dataOffset + (uint32_t)fileRow * rowBytes + (uint32_t)col * 3);
				f.read(rgb, n * 3);

				for (int16_t k = 0; k < n; k++) {
					px[k] = rgb565(rgb[k * 3 + 2], rgb[k * 3 + 1], rgb[k * 3 + 0]);
				}

				tft.startWrite(x0 + col, y0 + row, n, 1);
				tft.writePixels(px, n);
				tft.endWrite();
			}
		}
	}
#endif

	f.close();
	return true;
}

// Appends one timestamped line to LOG_FILE. Opened and closed per line
// so an entry is safely on the card before the next image is drawn --
// pulling the power mid-slideshow should not cost the log up to that
// point. FILE_WRITE carries O_CREAT|O_APPEND, so the file is made on
// first use and never truncated.
//
// millis() restarts from zero every run, which is what the restart
// marker written by setup() is for: it separates one run's timestamps
// from the next in a file that outlives both.
static const __FlashStringHelper *paintDirName(void)
{
	switch (paintDir) {
		case PAINT_BOTTOM_UP:  return F("bottom-up");
		case PAINT_LEFT_RIGHT: return F("left-to-right");
		case PAINT_RIGHT_LEFT: return F("right-to-left");
		default:               return F("top-down");
	}
}

static void logEvent(const __FlashStringHelper *what, const char *name)
{
	File f = SD.open(LOG_FILE, FILE_WRITE);
	if (!f) {
		// Reported rather than swallowed: a log that quietly stops is
		// worse than no log, since it looks like nothing happened.
		Serial.print(F("could not append to "));
		Serial.println(LOG_FILE);
		return;
	}

	f.print(millis());
	f.print(F(" ms  "));
	f.print(what);
	if (name) {
		f.print(name);
	}
	f.println();
	f.close();
}

static void showCurrentBmp(void)
{
	if (bmpCount == 0) {
		return;
	}

	if (pendingDir == PAINT_AUTO) {
		paintDir = (DRAW_BOTTOM_UP ^ ((bmpIndex & 1) != 0)) ? PAINT_BOTTOM_UP : PAINT_TOP_DOWN;
	} else {
		paintDir = pendingDir;
	}
	pendingDir = PAINT_AUTO; // one-shot: only the gesture that asked for it

	// No erase first: the incoming picture simply overwrites the old one
	// row by row, which drops a full-screen black flash between images and
	// makes the wipe direction above the visible transition. The trade-off
	// is that a picture smaller than the screen leaves whatever was around
	// it still showing.
	Serial.print(F("drawing "));
	Serial.println(bmpName[bmpIndex]);

	File f;
	bool opened;
	if (usingPlaylist) {
		f = SD.open(bmpName[bmpIndex]);
		opened = f;
	} else {
		opened = openBmpByIndex(bmpIndex, f);
	}

	unsigned long drawT0 = millis();
	bool drawn = opened && drawBmp(f, 0, 0);
	unsigned long drawMs = millis() - drawT0;

	Serial.print(F("  "));
	Serial.print(paintDirName());
	Serial.print(F(", "));
	Serial.print(drawMs);
	Serial.println(F(" ms"));

	if (!drawn) {
		tft.fillScreen(ST7789_RED); // visible failure indicator
	}

	logEvent(drawn ? F("drew ") : F("FAILED "), bmpName[bmpIndex]);
}

// Both lists share bmpName[]: re-reading is cheap next to a second
// array, and a second one would not fit alongside the first in AVR's
// 2KB. Switching modes therefore means re-parsing, not swapping a
// pointer.
static void loadNormalList(void)
{
	usingPlaylist = loadList(KEY_PLAYLIST);
	if (!usingPlaylist) {
		scanBmpFiles();
	}
}

static void enterSaver(void)
{
	savedIndex = bmpIndex;

	if (!loadList(KEY_SAVER)) {
		// No usable saver list. collectArray() only clears bmpName[] once
		// it has actually opened the file, but it can still have done so
		// before failing (an empty or unparseable list), so put the normal
		// list back either way. Treating this as an event defers the next
		// attempt by another idle timeout instead of retrying every pass.
		loadNormalList();
		lastEventMs = millis();
		return;
	}

	saverActive = true;
	usingPlaylist = true;   // the saver list holds paths, same as the playlist
	bmpIndex = 0;
	lastSaverStepMs = millis();
	Serial.println(F("idle -> screen saver"));
	showCurrentBmp();
}

static void exitSaver(void)
{
	saverActive = false;
	loadNormalList();
	bmpIndex = (savedIndex < bmpCount) ? savedIndex : 0;
	Serial.println(F("woken -> back to the normal list"));
	showCurrentBmp();
}

// True once per press, on the falling edge; the release only re-arms it.
// A level that has not held for BUTTON_DEBOUNCE_MS is ignored outright
// rather than delaying the edge, so a real press is still reported the
// moment it is seen. Each button keeps its own pair of state variables,
// so a bounce on one does not blank the other out -- passed in
// individually rather than as a little struct, since the IDE inserts its
// generated prototypes above anything a .ino declares.
static bool buttonPressed(uint8_t pin, bool &down, unsigned long &lastChangeMs)
{
	bool now = (digitalRead(pin) == LOW); // switch to ground, so LOW = held

	if (now == down || millis() - lastChangeMs < BUTTON_DEBOUNCE_MS) {
		return false;
	}

	down         = now;
	lastChangeMs = millis();
	return now;
}

// SW2/SW3 navigation. Clicks are counted rather than acted on as they
// arrive: each one adds a step and restarts a MULTI_CLICK_MS timer, and
// only once that expires is the whole count applied in a single move. A
// double click therefore lands two images away without drawing the one in
// between -- which matters here, where a draw costs over 100ms and would
// otherwise have to finish before the second click could even be seen.
//
// The count is kept as a signed number of steps, so SW2 and SW3 within
// the same window subtract from each other and cancel out exactly if
// pressed the same number of times. Nothing depends on that, but it beats
// having to decide which of the two a mixed sequence "meant".
static void pollButtons(void)
{
	static bool          prevDown = false, nextDown = false;
	static unsigned long prevChangeMs = 0, nextChangeMs = 0;
	static int8_t        pendingSteps = 0;
	static uint8_t       clickCount   = 0;
	static unsigned long lastClickMs  = 0;

	int8_t delta   = 0;
	bool   clicked = false;

	if (buttonPressed(SW_PREV, prevDown, prevChangeMs)) {
		delta--;
		clicked = true;
	}
	if (buttonPressed(SW_NEXT, nextDown, nextChangeMs)) {
		delta++;
		clicked = true;
	}

	if (clicked) {
		lastEventMs = millis();

		if (saverActive) {
			// Wakes only, like the first touch does: the click that woke
			// the saver is spent doing so and does not also move.
			exitSaver();
			pendingSteps = 0;
			clickCount   = 0;
			return;
		}

		pendingSteps += delta;
		if (clickCount < 255) {
			clickCount++; // only ever compared against zero above 1, so
			              // saturating beats wrapping back to "no clicks"
		}
		lastClickMs = millis();
		return;
	}

	if (clickCount == 0 || millis() - lastClickMs < MULTI_CLICK_MS) {
		return;
	}

	int8_t  steps = pendingSteps;
	uint8_t count = clickCount;

	pendingSteps = 0;
	clickCount   = 0;

	// Cast to int so these pick Print's integer overload rather than its
	// character one: int8_t and uint8_t are (signed/unsigned) char.
	Serial.print((int)count);
	Serial.print(F(" click(s) -> "));
	if (steps == 0) {
		Serial.println(F("no move"));
		return;
	}
	Serial.print(steps > 0 ? F("forward ") : F("back "));
	Serial.println((int)(steps > 0 ? steps : -steps));

	// Signed arithmetic in int16_t: bmpIndex is unsigned and steps may be
	// negative, and C's % keeps the sign of the dividend, so the result is
	// brought back into range explicitly rather than relying on the
	// (bmpIndex + bmpCount - 1) trick used for single steps elsewhere.
	int16_t idx = (int16_t)bmpIndex + steps;
	int16_t n   = (int16_t)bmpCount;

	idx %= n;
	if (idx < 0) {
		idx += n;
	}
	bmpIndex = (uint8_t)idx;

	showCurrentBmp();
}

void setup(void)
{
	Serial.begin(115200);
	while (!Serial)
		;
	
	// Internal pull-ups, on top of whatever the board already fits: both
	// switches only ever pull their pin down, so an undriven input would
	// otherwise float.
	pinMode(SW_PREV, INPUT_PULLUP);
	pinMode(SW_NEXT, INPUT_PULLUP);

	tft.begin();
	tft.setRotation(1); // landscape, 320x240
	touch.begin();
	// See TouchPaint.ino for why swapXY+invertX (not invertY): confirmed
	// against real hardware in the Zephyr port of this same shield.
	touch.setCalibration(0, 4095, 0, 4095, /*swapXY=*/true, /*invertX=*/true, /*invertY=*/false);

	tft.fillScreen(ST7789_BLACK);

	if (!SD.begin(SD_CS)) {
		Serial.println(F("SD.begin() failed -- check card is inserted and formatted FAT16/FAT32"));
		return;
	}

	logEvent(F("---- restart ----"), nullptr);

	// Settings first: "reverse" has to be known before any list is built.
	loadSettings();

	loadNormalList();

	Serial.println(portraitMode ? F("portrait mode: touch upper / lower half")
	                            : F("landscape mode: swipe left / right"));
	Serial.print(F("screen saver after "));
	Serial.print(idleTimeoutMs);
	Serial.print(F(" ms, advancing every "));
	Serial.print(saverIntervalMs);
	Serial.println(F(" ms"));
	Serial.println(reverseOrder ? F("list order: reversed")
	                            : F("list order: as written"));
	Serial.println(F("SW2 = previous, SW3 = next; click n times to move n images"));

	lastEventMs = millis();
	showCurrentBmp();
}

void loop(void)
{
	static bool wasTouched = false;
	static bool longPressFired = false;
	static bool wokeFromSaver = false;
	static uint16_t startX = 0, startY = 0;
	static uint16_t lastX = 0, lastY = 0;
	static unsigned long pressStartMs = 0;

	if (bmpCount == 0) {
		return;
	}

	// Before the touch handling, which returns early down several paths.
	pollButtons();

	uint16_t x, y;
	bool isTouched = touch.getPoint(x, y, tft.width(), tft.height());

	if (isTouched) {
		lastEventMs = millis();

		if (saverActive) {
			// First touch only wakes: it restores the normal list and the
			// image that was showing, and the gesture it belongs to is
			// discarded rather than also being acted on.
			exitSaver();
			wokeFromSaver = true;
		}

		if (!wasTouched) {
			startX = x;
			startY = y;
			pressStartMs = millis();
			longPressFired = false;
		}
		lastX = x;
		lastY = y;
		wasTouched = true;

		// Long press fires once, while still held, as soon as the hold
		// has stayed within LONG_PRESS_TOLERANCE of its start point for
		// LONG_PRESS_MS -- not on release, so it doesn't wait for a
		// finger lift that a genuine long press may not do for a while.
		bool stationary = abs((int16_t)x - (int16_t)startX) < LONG_PRESS_TOLERANCE
		                   && abs((int16_t)y - (int16_t)startY) < LONG_PRESS_TOLERANCE;
		if (!wokeFromSaver && !longPressFired && stationary && millis() - pressStartMs >= LONG_PRESS_MS) {
			longPressFired = true;
			Serial.println(F("long press -> back to first image"));
			bmpIndex = 0;
			showCurrentBmp();
		}
		return;
	}

	if (!wasTouched) {
		// Nothing released to act on, so this is the idle path.
		if (saverActive) {
			if (millis() - lastSaverStepMs >= saverIntervalMs) {
				lastSaverStepMs = millis();
				bmpIndex = (bmpIndex + 1) % bmpCount;
				showCurrentBmp();
			}
		} else if (millis() - lastEventMs >= idleTimeoutMs) {
			enterSaver();
		}
		return;
	}
	wasTouched = false;
	lastEventMs = millis();

	if (wokeFromSaver) {
		wokeFromSaver = false;
		return; // that touch was spent waking up
	}

	if (longPressFired) {
		return; // already handled as a long press while it was held
	}

	int16_t dx = (int16_t)lastX - (int16_t)startX;
	int16_t dy = (int16_t)lastY - (int16_t)startY;

	if (portraitMode) {
		// Portrait: where the touch landed decides, not which way it
		// moved -- the board is being held on its side, so a flick along
		// the panel's long axis is an awkward gesture. Touch the top half
		// and the next picture wipes in downwards; touch the bottom half
		// and the previous one wipes in upwards. The wipe direction is
		// part of the gesture here, so it is set either way rather than
		// only under SWIPE_WIPES_SIDEWAYS.
		bool upper = (startY < (uint16_t)(tft.height() / 2));

		if (upper) {
			bmpIndex = (bmpIndex + 1) % bmpCount;
			pendingDir = PAINT_TOP_DOWN;
		} else {
			bmpIndex = (bmpIndex + bmpCount - 1) % bmpCount;
			pendingDir = PAINT_BOTTOM_UP;
		}
		Serial.println(upper ? F("touch upper -> next, wiping down")
		                     : F("touch lower -> previous, wiping up"));
	} else if (abs(dx) >= SWIPE_THRESHOLD && abs(dx) >= abs(dy)) {
		// horizontal swipe: left -> next, right -> previous
		if (dx < 0) {
			bmpIndex = (bmpIndex + 1) % bmpCount;
		} else {
			bmpIndex = (bmpIndex + bmpCount - 1) % bmpCount;
		}
#if SWIPE_WIPES_SIDEWAYS
		// Wipe the way the finger went, so the transition follows the
		// gesture instead of the index-alternating default.
		pendingDir = (dx < 0) ? PAINT_RIGHT_LEFT : PAINT_LEFT_RIGHT;
#endif
		Serial.println(dx < 0 ? F("swipe left") : F("swipe right"));
	} else {
		// tap (or a swipe too short/too vertical to count)
		bmpIndex = (bmpIndex + 1) % bmpCount;
		Serial.println(F("tap"));
	}

	showCurrentBmp();

	delay(300); // simple debounce against one release registering as several
}
