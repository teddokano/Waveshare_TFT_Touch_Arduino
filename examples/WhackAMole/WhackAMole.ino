/*
 * WhackAMole -- Waveshare_TFT_Touch library example
 *
 * A tap-the-mole game for the Waveshare 2.8inch TFT Touch Shield
 * (Rev2.1: ST7789V + XPT2046).
 * https://www.waveshare.com/2.8inch-tft-touch-shield.htm
 *
 * Shield pins (fixed on the PCB, Arduino Uno R3 header):
 *   LCD_CS  -> D10   LCD_DC -> D7   LCD_BL -> D9
 *   TP_CS   -> D4    TP_IRQ -> D3
 *   SCLK/MOSI/MISO -> D13/D11/D12 (hardware SPI, shared by LCD + touch)
 *
 * How to play:
 *   - Touch the screen to start.
 *   - Moles pop out of the twelve holes. Tap one while it is up to score.
 *   - They stay up for less and less time as the 30 seconds run down, and
 *     a third one joins in after ten seconds.
 *   - Missing costs nothing, so tap away.
 *
 * Why this game suits this panel:
 *   The touch panel is resistive and single-point, and it drops a sizeable
 *   share of its readings while a finger is moving -- measured at 30-50%
 *   of polls on real hardware. A game made of taps on big targets is
 *   nearly immune to both: a hole is 80x71 pixels, far larger than a
 *   fingertip's 45-50px contact patch, and a tap only has to be seen on
 *   one of the frames the finger is down.
 *
 * Implementation notes:
 *   - The library has no text rendering, so this sketch carries its own
 *     tiny 3x5 dot font (digits plus the letters the game needs), drawn
 *     with fillRect() at an integer scale. Same font as the Breakout
 *     example, with I and W added and the letters it doesn't use dropped.
 *   - Nothing is redrawn wholesale per frame. A mole appearing, being
 *     bonked or going back down repaints one 60px circle; the score and
 *     clock repaint only their own digits. On an idle frame no SPI
 *     traffic happens at all.
 *   - Per-cell state is one byte plus one deadline each, so the whole
 *     game fits in a couple of hundred bytes of RAM and runs on the UNO
 *     R3 as well as on the FRDM-MCX boards.
 */

#include <Arduino.h>
#include <SPI.h>
#include <ST7789.h>
#include <XPT2046.h>

ST7789 tft(D10, D7, D9);
XPT2046 touch(D4, D3);

// ---- layout (portrait, 240x320) ------------------------------------

static const int16_t SCREEN_W = 240;
static const int16_t SCREEN_H = 320;

static const int16_t HUD_H = 26; // score and clock band at the top

static const uint8_t GRID_COLS = 3;
static const uint8_t GRID_ROWS = 4;
static const uint8_t CELLS = GRID_COLS * GRID_ROWS;

static const int16_t CELL_W = SCREEN_W / GRID_COLS; // 80
static const int16_t GRID_TOP = 28;

// The bottom rows of pixels are deliberately left outside the grid, and
// every tap landing there is thrown away. On the UNO R3 this panel
// occasionally answers an SPI read with all zeroes, which the driver
// cannot tell from a real touch: the pressure figure of merit comes out
// at its maximum and the coordinates at raw (0,0), which this sketch's
// calibration maps to exactly (239, 319) -- the bottom-right pixel.
// Keeping the grid clear of that corner turns those phantom touches into
// taps on dead space instead of free hits, and stops them from starting
// a game by themselves on the title screen. It costs 8 of 320 rows.
static const int16_t DEAD_H = 8;

static const int16_t CELL_H = (SCREEN_H - DEAD_H - GRID_TOP) / GRID_ROWS; // 71
static const int16_t GRID_BOTTOM = GRID_TOP + GRID_ROWS * CELL_H;         // 312

static const int16_t HOLE_R = 30;
static const int16_t MOLE_R = 24;

static const uint8_t FRAME_MS = 20;

// ---- game pacing ----------------------------------------------------

static const uint32_t GAME_MS = 30000;

static const uint16_t POP_MS_START = 800; // gap between moles appearing
static const uint16_t POP_MS_END = 300;
static const uint16_t UP_MS_START = 1200; // how long one stays up
static const uint16_t UP_MS_END = 500;
static const uint16_t BONK_MS = 200;      // how long a hit mole is shown hit

static const uint16_t OVER_LOCKOUT_MS = 1500;

#define COLOR_BG    ST7789_BLACK
#define COLOR_HUD   ST7789_WHITE
#define COLOR_LINE  ST7789_GREY
#define COLOR_HOLE  0x3103 // dark brown, the empty hole
#define COLOR_MOLE  0xAB88 // mole brown
#define COLOR_SNOUT 0xF514 // pink
#define COLOR_BONK  ST7789_RED
#define COLOR_RING  ST7789_YELLOW

// Declared up here, above the first function in the file, because
// setState() takes one as a parameter. The IDE generates prototypes for
// every function in a .ino and inserts them immediately before the first
// function definition, so a type declared after that point does not
// exist yet when its own function's prototype is compiled -- the error
// is "variable or field 'setState' declared void", pointing at the
// definition rather than at the real problem. The rest of the game state
// lives further down with the other globals.
enum GameState { STATE_TITLE, STATE_PLAY, STATE_OVER };

// ---- tiny 3x5 font -------------------------------------------------
//
// Each glyph is 15 bits, row-major from the top-left, MSB first:
// bit 14 is (0,0), bit 12 is (2,0), bit 0 is (2,4).

static const uint16_t DIGIT_GLYPHS[10] = {
	0x7B6F, // 0  111 101 101 101 111
	0x2C97, // 1  010 110 010 010 111
	0x73E7, // 2  111 001 111 100 111
	0x73CF, // 3  111 001 111 001 111
	0x5BC9, // 4  101 101 111 001 001
	0x79CF, // 5  111 100 111 001 111
	0x79EF, // 6  111 100 111 101 111
	0x7249, // 7  111 001 001 001 001
	0x7BEF, // 8  111 101 111 101 111
	0x7BCF, // 9  111 101 111 001 111
};

// Only the letters that appear in WHACK, A MOLE, TOUCH TO START and
// TIME UP. At 3x5 some letters cannot be told apart from a digit, so O
// reuses 0's bits and S reuses 5's.
static uint16_t letterGlyph(char c)
{
	switch (c) {
	case 'A': return 0x7BED; // 111 101 111 101 101
	case 'C': return 0x7927; // 111 100 100 100 111
	case 'E': return 0x79E7; // 111 100 111 100 111
	case 'H': return 0x5BED; // 101 101 111 101 101
	case 'I': return 0x7497; // 111 010 010 010 111
	case 'K': return 0x5BAD; // 101 101 110 101 101
	case 'L': return 0x4927; // 100 100 100 100 111
	case 'M': return 0x5FED; // 101 111 111 101 101
	case 'O': return 0x7B6F; // 111 101 101 101 111
	case 'P': return 0x7BE4; // 111 101 111 100 100
	case 'R': return 0x7BF5; // 111 101 111 110 101
	case 'S': return 0x79CF; // 111 100 111 001 111
	case 'T': return 0x7492; // 111 010 010 010 010
	case 'U': return 0x5B6F; // 101 101 101 101 111
	case 'W': return 0x5BFD; // 101 101 111 111 101
	default:  return 0;      // space and anything unknown
	}
}

static void drawGlyph(int16_t x, int16_t y, uint16_t glyph, uint8_t scale, uint16_t color)
{
	for (uint8_t row = 0; row < 5; row++) {
		for (uint8_t col = 0; col < 3; col++) {
			if (glyph & (1u << (14 - (row * 3 + col))))
				tft.fillRect(x + col * scale, y + row * scale, scale, scale, color);
		}
	}
}

static int16_t textWidth(const char *s, uint8_t scale)
{
	int16_t n = 0;
	while (s[n])
		n++;
	return n ? n * 4 * scale - scale : 0;
}

static void drawText(int16_t x, int16_t y, const char *s, uint8_t scale, uint16_t color)
{
	for (; *s; s++, x += 4 * scale) {
		char c = *s;
		uint16_t glyph = (c >= '0' && c <= '9') ? DIGIT_GLYPHS[c - '0'] : letterGlyph(c);
		drawGlyph(x, y, glyph, scale, color);
	}
}

static void drawTextCentered(int16_t y, const char *s, uint8_t scale, uint16_t color)
{
	drawText((SCREEN_W - textWidth(s, scale)) / 2, y, s, scale, color);
}

// Fixed-width, zero-padded number; clears its own background first so it
// can be redrawn in place.
static void drawNumber(int16_t x, int16_t y, uint16_t value, uint8_t digits, uint8_t scale, uint16_t color)
{
	tft.fillRect(x, y, digits * 4 * scale - scale, 5 * scale, COLOR_BG);
	for (int8_t i = digits - 1; i >= 0; i--) {
		drawGlyph(x + i * 4 * scale, y, DIGIT_GLYPHS[value % 10], scale, color);
		value /= 10;
	}
}

// Same, but no leading zeros and centred -- for the final score.
static void drawNumberCentered(int16_t y, uint16_t value, uint8_t scale, uint16_t color)
{
	uint8_t digits = (value >= 100) ? 3 : (value >= 10) ? 2 : 1;
	drawNumber((SCREEN_W - (digits * 4 * scale - scale)) / 2, y, value, digits, scale, color);
}

// ---- game state ----------------------------------------------------
//
// enum GameState is declared near the top of the file, not here -- see
// the comment there for why.

static const uint8_t CELL_EMPTY = 0;
static const uint8_t CELL_UP = 1;
static const uint8_t CELL_BONKED = 2;

static GameState state;
static uint32_t stateSince;
static uint32_t lastFrame;

static uint8_t cellState[CELLS];
static uint32_t cellUntil[CELLS]; // when this cell's state expires

static uint32_t gameStart;
static uint32_t nextPopAt;
static uint16_t score;
static uint8_t shownSecs; // what the clock currently reads, to skip redraws

// Touch edge detection. pressed stays true across the frames one finger
// is down; misses counts consecutive frames with no reading, so a few
// dropped polls in the middle of a press don't look like a release.
static bool pressed;
static uint8_t misses;
static const uint8_t RELEASE_FRAMES = 3;

// Small xorshift PRNG, seeded from micros() at the moment the player
// first touches the screen -- avoids depending on random()/analogRead()
// noise, which not every core provides the same way.
static uint16_t rngState = 0xACE1;

static uint16_t rng(void)
{
	rngState ^= rngState << 7;
	rngState ^= rngState >> 9;
	rngState ^= rngState << 8;
	return rngState;
}

static void setState(GameState s)
{
	state = s;
	stateSince = millis();
}

// ---- drawing -------------------------------------------------------

static int16_t cellCX(uint8_t i)
{
	return (i % GRID_COLS) * CELL_W + CELL_W / 2;
}

static int16_t cellCY(uint8_t i)
{
	return GRID_TOP + (i / GRID_COLS) * CELL_H + CELL_H / 2;
}

// An empty hole. Also the eraser: a mole, ears and all, is drawn inside
// this circle, so repainting it removes whatever was there.
static void drawHoleAt(int16_t cx, int16_t cy)
{
	tft.fillCircle(cx, cy, HOLE_R, COLOR_HOLE);
}

static void drawMoleAt(int16_t cx, int16_t cy, bool bonked)
{
	uint16_t head = bonked ? COLOR_BONK : COLOR_MOLE;

	// Ears first, so the head overlaps them and they read as behind it.
	tft.fillCircle(cx - 16, cy - 18, 7, head);
	tft.fillCircle(cx + 16, cy - 18, 7, head);
	tft.fillCircle(cx, cy, MOLE_R, head);

	tft.fillCircle(cx, cy + 9, 8, COLOR_SNOUT);
	tft.fillCircle(cx, cy + 4, 3, ST7789_BLACK);

	if (bonked) {
		// X eyes, plus a ring for the impact. The ring's radius stays
		// inside HOLE_R so drawHoleAt() still erases all of it.
		for (int8_t s = -1; s <= 1; s += 2) {
			int16_t ex = cx + s * 9;
			int16_t ey = cy - 7;
			tft.drawLine(ex - 4, ey - 4, ex + 4, ey + 4, ST7789_WHITE);
			tft.drawLine(ex - 4, ey + 4, ex + 4, ey - 4, ST7789_WHITE);
		}
		tft.drawCircle(cx, cy, MOLE_R + 3, COLOR_RING);
	} else {
		tft.fillCircle(cx - 9, cy - 7, 3, ST7789_BLACK);
		tft.fillCircle(cx + 9, cy - 7, 3, ST7789_BLACK);
	}
}

static void drawCell(uint8_t i)
{
	int16_t cx = cellCX(i);
	int16_t cy = cellCY(i);

	if (cellState[i] == CELL_EMPTY)
		drawHoleAt(cx, cy);
	else
		drawMoleAt(cx, cy, cellState[i] == CELL_BONKED);
}

static void drawScore(void)
{
	drawNumber(4, 5, score, 3, 3, COLOR_HUD);
}

static void drawClock(uint8_t secs)
{
	drawNumber(SCREEN_W - 4 - (2 * 4 * 3 - 3), 5, secs, 2, 3, COLOR_HUD);
	shownSecs = secs;
}

static void drawHud(void)
{
	tft.fillRect(0, 0, SCREEN_W, HUD_H, COLOR_BG);
	drawScore();
	drawClock(shownSecs);
	tft.drawFastHLine(0, HUD_H - 1, SCREEN_W, COLOR_LINE);
}

static void drawField(void)
{
	tft.fillScreen(COLOR_BG);
	drawHud();
	for (uint8_t i = 0; i < CELLS; i++)
		drawCell(i);
}

// ---- game logic ----------------------------------------------------

// Linear ramp from a down to b over the length of one game.
static uint16_t ramp(uint16_t a, uint16_t b, uint32_t elapsed)
{
	if (elapsed >= GAME_MS)
		return b;
	return (uint16_t)(a - (uint32_t)(a - b) * elapsed / GAME_MS);
}

static uint8_t molesUp(void)
{
	uint8_t n = 0;
	for (uint8_t i = 0; i < CELLS; i++)
		if (cellState[i] == CELL_UP)
			n++;
	return n;
}

// Raises a mole in a random empty hole. Picks one index and walks
// forward from there, so it never loops forever even if the grid fills.
static void popMole(uint32_t now, uint32_t elapsed)
{
	uint8_t start = rng() % CELLS;
	for (uint8_t n = 0; n < CELLS; n++) {
		uint8_t i = (start + n) % CELLS;
		if (cellState[i] != CELL_EMPTY)
			continue;
		cellState[i] = CELL_UP;
		cellUntil[i] = now + ramp(UP_MS_START, UP_MS_END, elapsed);
		drawCell(i);
		return;
	}
}

// A tap at (tx, ty). Hitting a mole scores; everything else is ignored.
// A miss costs nothing on purpose: this panel can emit a stray touch of
// its own (see the note on the dead strip above), and nothing the player
// did not do should ever be able to take points away. It also means a
// press whose readings flicker, and so looks like two taps, cannot score
// twice -- the second one lands on a cell that is already CELL_BONKED.
static void handleTap(uint16_t tx, uint16_t ty, uint32_t now)
{
	if (ty < GRID_TOP || ty >= GRID_BOTTOM)
		return;

	uint8_t col = (uint8_t)(tx / CELL_W);
	uint8_t row = (uint8_t)((ty - GRID_TOP) / CELL_H);
	if (col >= GRID_COLS || row >= GRID_ROWS)
		return;

	uint8_t i = row * GRID_COLS + col;
	if (cellState[i] != CELL_UP)
		return;

	score++;
	drawScore();
	cellState[i] = CELL_BONKED;
	cellUntil[i] = now + BONK_MS;
	drawCell(i);
}

// Reads the panel and reports a tap only on the frame the finger first
// lands, with tx/ty set. Both are left alone when it returns false.
static bool tapped(uint16_t &tx, uint16_t &ty)
{
	if (touch.getPoint(tx, ty, SCREEN_W, SCREEN_H)) {
		misses = 0;
		if (pressed)
			return false;
		pressed = true;
		return true;
	}

	if (pressed && ++misses >= RELEASE_FRAMES)
		pressed = false;
	return false;
}

static void clearCells(void)
{
	for (uint8_t i = 0; i < CELLS; i++)
		cellState[i] = CELL_EMPTY;
}

static void showTitle(void)
{
	clearCells();
	score = 0;
	shownSecs = GAME_MS / 1000;

	tft.fillScreen(COLOR_BG);
	drawTextCentered(50, "WHACK", 6, ST7789_WHITE);
	drawTextCentered(95, "A MOLE", 6, ST7789_WHITE);
	drawMoleAt(SCREEN_W / 2, 190, false);
	drawTextCentered(260, "TOUCH TO START", 2, ST7789_CYAN);
	setState(STATE_TITLE);
}

static void newGame(void)
{
	clearCells();
	score = 0;
	shownSecs = GAME_MS / 1000;
	drawField();

	uint32_t now = millis();
	gameStart = now;
	nextPopAt = now + 400; // a beat to get a finger ready
	setState(STATE_PLAY);
	Serial.println("start");
}

static void endGame(void)
{
	tft.fillScreen(COLOR_BG);
	drawTextCentered(80, "TIME UP", 4, ST7789_WHITE);
	drawNumberCentered(140, score, 8, COLOR_RING);
	drawTextCentered(260, "TOUCH TO START", 2, ST7789_CYAN);
	setState(STATE_OVER);

	Serial.print("score ");
	Serial.println(score);
}

static void stepGame(uint32_t now)
{
	uint32_t elapsed = now - gameStart;
	if (elapsed >= GAME_MS) {
		endGame();
		return;
	}

	uint8_t secs = (uint8_t)((GAME_MS - elapsed + 999) / 1000);
	if (secs != shownSecs)
		drawClock(secs);

	uint16_t tx, ty;
	if (tapped(tx, ty))
		handleTap(tx, ty, now);

	// Moles that were not hit go back down; hit ones finish their flash.
	for (uint8_t i = 0; i < CELLS; i++) {
		if (cellState[i] != CELL_EMPTY && (int32_t)(now - cellUntil[i]) >= 0) {
			cellState[i] = CELL_EMPTY;
			drawCell(i);
		}
	}

	// Two at a time to begin with, three once the player has the idea.
	uint8_t maxUp = (elapsed < 10000) ? 2 : 3;
	if ((int32_t)(now - nextPopAt) >= 0 && molesUp() < maxUp) {
		popMole(now, elapsed);
		nextPopAt = now + ramp(POP_MS_START, POP_MS_END, elapsed);
	}
}

// ---- Arduino entry points -------------------------------------------

void setup(void)
{
	Serial.begin(115200);
	Serial.println("WhackAMole");

	tft.begin();
	// Portrait, 240x320. Rotation 0 writes straight through (MADCTL
	// 0x00), so the screen's bottom edge is the panel's own bottom.
	tft.setRotation(0);
	touch.begin();

	// The touch panel does not move when the display's addressing does,
	// so these flags are rotation 1's mapping turned 90 degrees with it.
	// TouchPaint's rotation-1 calibration resolves to
	//   screen x = 319 - map(rawY),  screen y = map(rawX)
	// and turning the picture clockwise sends a point at (xl,yl) to
	// (239 - yl, xl), which gives
	//   screen x = 239 - map(rawX),  screen y = 319 - map(rawY)
	// -- the raw axes no longer need swapping, and both now invert.
	// Run the TouchCalibration example if taps do not land where you
	// expect them to near the edges.
	touch.setCalibration(0, 4095, 0, 4095, /*swapXY=*/false, /*invertX=*/true, /*invertY=*/true);

	showTitle();
	lastFrame = millis();
}

void loop(void)
{
	uint32_t now = millis();
	if (now - lastFrame < FRAME_MS)
		return;
	lastFrame = now;

	uint16_t tx, ty;

	switch (state) {
	case STATE_TITLE:
		if (tapped(tx, ty) && ty < GRID_BOTTOM) {
			rngState ^= (uint16_t)micros();
			if (rngState == 0)
				rngState = 0xACE1;
			newGame();
		}
		break;

	case STATE_PLAY:
		stepGame(now);
		break;

	case STATE_OVER:
		// Short lockout so the tap that was aimed at the last mole
		// doesn't immediately start another game.
		if (now - stateSince > OVER_LOCKOUT_MS && tapped(tx, ty) && ty < GRID_BOTTOM)
			showTitle();
		break;
	}
}
