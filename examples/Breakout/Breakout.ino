/*
 * Breakout -- Waveshare_TFT_Touch library example
 *
 * A small block-breaking game for the Waveshare 2.8inch TFT Touch
 * Shield (Rev2.1: ST7789V + XPT2046).
 * https://www.waveshare.com/2.8inch-tft-touch-shield.htm
 *
 * Shield pins (fixed on the PCB, Arduino Uno R3 header):
 *   LCD_CS  -> D10   LCD_DC -> D7   LCD_BL -> D9
 *   TP_CS   -> D4    TP_IRQ -> D3
 *   SCLK/MOSI/MISO -> D13/D11/D12 (hardware SPI, shared by LCD + touch)
 *
 * How to play:
 *   - Touch the screen to start.
 *   - Slide a finger/stylus left and right anywhere on the panel; the
 *     paddle follows the horizontal position of the touch.
 *   - Where the ball lands on the paddle sets its bounce angle: the
 *     centre sends it straight up, the edges send it out steeply.
 *   - Clear every brick to go to the next level (the ball gets faster).
 *     Three balls per game.
 *
 * Implementation notes:
 *   - The library has no text rendering, so this sketch carries its own
 *     tiny 3x5 dot font (digits plus the handful of letters the game
 *     needs), drawn with fillRect() at an integer scale.
 *   - Nothing is redrawn wholesale per frame. Only what changed is
 *     touched: the ball's old 6x6 square is erased and the new one
 *     drawn, the paddle only paints/erases the strip it moved by, and a
 *     hit brick is filled black once. That keeps the SPI traffic per
 *     frame tiny, so the same sketch runs smoothly on UNO R3 as well as
 *     the FRDM-MCX boards.
 *   - Ball position/velocity are 12.4 fixed point (1/16 pixel) ints, no
 *     floating point. The ball never moves more than 6px per frame, less
 *     than both its own size and a brick's height, so it can't tunnel
 *     through a brick between frames.
 *   - State is kept small (bricks are one 16-bit mask per row) so it fits
 *     comfortably in the UNO R3's 2KB of RAM.
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

static const int16_t HUD_H = 20; // score/level/lives band at the top

static const uint8_t BRICK_COLS = 10;
static const uint8_t BRICK_ROWS = 6;
static const int16_t BRICK_W = SCREEN_W / BRICK_COLS; // 24
static const int16_t BRICK_H = 12;
static const int16_t BRICK_TOP = HUD_H + 16;
static const int16_t BRICK_BOTTOM = BRICK_TOP + BRICK_ROWS * BRICK_H;

// 15% of the screen width, the share it had in landscape -- 48 on a
// 240-wide panel would be a fifth of it, and two bricks wide.
static const int16_t PADDLE_W = 36;
static const int16_t PADDLE_H = 6;

// The paddle takes its X from the touch panel and ignores the touch Y,
// so the screen can be driven from anywhere -- but a finger put down
// next to the paddle covers it. The panel's pixels are square, 5.6 per
// mm either way, so a fingertip's contact patch is 45-50px across: the
// band below the paddle has to be about that deep for the paddle to
// stay visible while it is being played. Portrait has the room for it
// -- it leaves 146px of ball travel below the bricks, more than the
// 116px landscape had before any band was reserved at all.
static const int16_t TOUCH_BAND_H = 50;
static const int16_t PADDLE_Y = SCREEN_H - TOUCH_BAND_H - PADDLE_H;

static const int16_t BALL_SIZE = 6;

static const uint8_t FP = 4; // fixed point fraction bits (1/16 px)
static const int16_t SPEED_BASE = 40;  // vertical speed at level 1 (2.5 px/frame)
static const int16_t SPEED_STEP = 6;   // added per level
static const int16_t SPEED_MAX = 96;   // 6 px/frame == BALL_SIZE, no tunnelling
static const uint16_t FRAME_MS = 15;

static const uint8_t START_LIVES = 3;
static const uint16_t SERVE_DELAY_MS = 1000;

#define COLOR_BG     ST7789_BLACK
#define COLOR_PADDLE ST7789_WHITE
#define COLOR_BALL   ST7789_YELLOW
#define COLOR_HUD    ST7789_WHITE
#define COLOR_LINE   ST7789_GREY

static const uint16_t ROW_COLORS[BRICK_ROWS] = {
	ST7789_RED,
	0xFC00, // orange
	ST7789_YELLOW,
	ST7789_GREEN,
	ST7789_CYAN,
	ST7789_MAGENTA,
};

// Declared up here, above the first function in the file, because
// setState() takes one as a parameter. The IDE generates prototypes for
// every function in a .ino and inserts them immediately before the
// first function definition, so a type declared after that point does
// not exist yet when its own function's prototype is compiled -- the
// error is "variable or field 'setState' declared void", pointing at
// the definition rather than at the real problem. The rest of the game
// state lives further down with the other globals.
enum GameState { STATE_TITLE, STATE_SERVE, STATE_PLAY, STATE_GAME_OVER };

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

static uint16_t letterGlyph(char c)
{
	switch (c) {
	case 'A': return 0x7BED; // 111 101 111 101 101
	case 'B': return 0x6BAE; // 110 101 110 101 110
	case 'C': return 0x7927; // 111 100 100 100 111
	case 'E': return 0x79E7; // 111 100 111 100 111
	case 'G': return 0x796F; // 111 100 101 101 111
	case 'H': return 0x5BED; // 101 101 111 101 101
	case 'K': return 0x5BAD; // 101 101 110 101 101
	case 'L': return 0x4927; // 100 100 100 100 111
	case 'M': return 0x5FED; // 101 111 111 101 101
	case 'O': return 0x7B6F; // 111 101 101 101 111
	case 'P': return 0x7BE4; // 111 101 111 100 100
	case 'R': return 0x7BF5; // 111 101 111 110 101
	case 'S': return 0x79CF; // 111 100 111 001 111
	case 'T': return 0x7492; // 111 010 010 010 010
	case 'U': return 0x5B6F; // 101 101 101 101 111
	case 'V': return 0x5B6A; // 101 101 101 101 010
	case 'Y': return 0x5A92; // 101 101 010 010 010
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

// Fixed-width, zero-padded number; clears its own background first so
// it can be redrawn in place.
static void drawNumber(int16_t x, int16_t y, uint32_t value, uint8_t digits, uint8_t scale, uint16_t color)
{
	tft.fillRect(x, y, digits * 4 * scale - scale, 5 * scale, COLOR_BG);
	for (int8_t i = digits - 1; i >= 0; i--) {
		drawGlyph(x + i * 4 * scale, y, DIGIT_GLYPHS[value % 10], scale, color);
		value /= 10;
	}
}

// ---- game state ----------------------------------------------------
//
// enum GameState is declared near the top of the file, not here -- see
// the comment there for why.

static GameState state;
static uint32_t stateSince;
static uint32_t lastFrame;

static uint16_t bricks[BRICK_ROWS]; // bit c set == brick at column c alive
static uint8_t bricksLeft;

static int16_t ballX, ballY;   // fixed point, top-left of the ball
static int16_t ballVX, ballVY; // fixed point per frame
static int16_t ballDrawnX, ballDrawnY;
static bool ballDrawn;

static int16_t paddleX; // left edge, pixels

// Where the finger was the last time the panel gave a reading, kept
// across frames so a dropped sample does not stop the paddle.
static int16_t paddleTarget;
static bool paddleTargetValid;

static uint16_t score;
static uint8_t lives;
static uint8_t level;

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

static void drawBrick(uint8_t row, uint8_t col, uint16_t color)
{
	int16_t x = col * BRICK_W;
	int16_t y = BRICK_TOP + row * BRICK_H;
	if (color == COLOR_BG) {
		tft.fillRect(x, y, BRICK_W, BRICK_H, COLOR_BG);
	} else {
		// 1px black gutter on the right/bottom so the bricks read as a grid
		tft.fillRect(x, y, BRICK_W - 1, BRICK_H - 1, color);
		tft.drawFastHLine(x, y + BRICK_H - 1, BRICK_W, COLOR_BG);
		tft.drawFastVLine(x + BRICK_W - 1, y, BRICK_H, COLOR_BG);
	}
}

static void drawAllBricks(void)
{
	for (uint8_t r = 0; r < BRICK_ROWS; r++)
		for (uint8_t c = 0; c < BRICK_COLS; c++)
			if (bricks[r] & (1u << c))
				drawBrick(r, c, ROW_COLORS[r]);
}

static void drawScore(void)
{
	drawNumber(4, 3, score, 5, 3, COLOR_HUD);
}

static void drawLevel(void)
{
	drawText(120, 3, "L", 3, COLOR_HUD);
	drawNumber(136, 3, level, 2, 3, COLOR_HUD);
}

static void drawLives(void)
{
	// One small ball per remaining life, right-aligned
	tft.fillRect(SCREEN_W - 60, 2, 60, HUD_H - 4, COLOR_BG);
	for (uint8_t i = 0; i < lives; i++)
		tft.fillRect(SCREEN_W - 12 - i * 14, 6, 8, 8, COLOR_BALL);
}

static void drawHud(void)
{
	tft.fillRect(0, 0, SCREEN_W, HUD_H, COLOR_BG);
	drawScore();
	drawLevel();
	drawLives();
	tft.drawFastHLine(0, HUD_H - 1, SCREEN_W, COLOR_LINE);
}

static void drawPaddle(void)
{
	tft.fillRect(paddleX, PADDLE_Y, PADDLE_W, PADDLE_H, COLOR_PADDLE);
}

// Moves the paddle, only painting/erasing the strip that changed.
static void movePaddle(int16_t nx)
{
	if (nx < 0)
		nx = 0;
	if (nx > SCREEN_W - PADDLE_W)
		nx = SCREEN_W - PADDLE_W;
	int16_t ox = paddleX;
	if (nx == ox)
		return;

	if (abs(nx - ox) >= PADDLE_W) {
		tft.fillRect(ox, PADDLE_Y, PADDLE_W, PADDLE_H, COLOR_BG);
		tft.fillRect(nx, PADDLE_Y, PADDLE_W, PADDLE_H, COLOR_PADDLE);
	} else if (nx > ox) {
		tft.fillRect(ox, PADDLE_Y, nx - ox, PADDLE_H, COLOR_BG);
		tft.fillRect(ox + PADDLE_W, PADDLE_Y, nx - ox, PADDLE_H, COLOR_PADDLE);
	} else {
		tft.fillRect(nx + PADDLE_W, PADDLE_Y, ox - nx, PADDLE_H, COLOR_BG);
		tft.fillRect(nx, PADDLE_Y, ox - nx, PADDLE_H, COLOR_PADDLE);
	}
	paddleX = nx;
}

static void eraseBall(void)
{
	if (!ballDrawn)
		return;
	tft.fillRect(ballDrawnX, ballDrawnY, BALL_SIZE, BALL_SIZE, COLOR_BG);
	ballDrawn = false;
	// The ball can overlap the paddle for a frame while bouncing off it;
	// erasing it must not leave a black bite in the paddle.
	if (ballDrawnY + BALL_SIZE > PADDLE_Y && ballDrawnY < PADDLE_Y + PADDLE_H)
		drawPaddle();
}

static void drawBall(void)
{
	int16_t x = ballX >> FP;
	int16_t y = ballY >> FP;
	if (ballDrawn && x == ballDrawnX && y == ballDrawnY)
		return;
	eraseBall();
	tft.fillRect(x, y, BALL_SIZE, BALL_SIZE, COLOR_BALL);
	ballDrawnX = x;
	ballDrawnY = y;
	ballDrawn = true;
}

// ---- game logic ----------------------------------------------------

static int16_t levelSpeed(void)
{
	int16_t s = SPEED_BASE + (level - 1) * SPEED_STEP;
	return s > SPEED_MAX ? SPEED_MAX : s;
}

static void resetBricks(void)
{
	for (uint8_t r = 0; r < BRICK_ROWS; r++)
		bricks[r] = (1u << BRICK_COLS) - 1;
	bricksLeft = BRICK_ROWS * BRICK_COLS;
}

// If pixel (px, py) is inside a live brick, knock it out and return true.
static bool hitBrickAt(int16_t px, int16_t py)
{
	if (py < BRICK_TOP || py >= BRICK_BOTTOM || px < 0 || px >= SCREEN_W)
		return false;
	uint8_t row = (py - BRICK_TOP) / BRICK_H;
	uint8_t col = px / BRICK_W;
	if (!(bricks[row] & (1u << col)))
		return false;

	bricks[row] &= ~(1u << col);
	bricksLeft--;
	drawBrick(row, col, COLOR_BG);
	score += (BRICK_ROWS - row) * 10; // top rows are worth more
	drawScore();
	return true;
}

// Checks the four corners of a ball whose top-left is at (px, py).
static bool hitBricks(int16_t px, int16_t py)
{
	bool hit = false;
	hit |= hitBrickAt(px, py);
	hit |= hitBrickAt(px + BALL_SIZE - 1, py);
	hit |= hitBrickAt(px, py + BALL_SIZE - 1);
	hit |= hitBrickAt(px + BALL_SIZE - 1, py + BALL_SIZE - 1);
	return hit;
}

static void placeBallOnPaddle(void)
{
	ballX = (paddleX + (PADDLE_W - BALL_SIZE) / 2) << FP;
	ballY = (PADDLE_Y - BALL_SIZE) << FP;
}

static void startServe(void)
{
	eraseBall();
	placeBallOnPaddle();
	drawBall();
	setState(STATE_SERVE);
}

static void launchBall(void)
{
	int16_t speed = levelSpeed();
	ballVY = -speed;
	// Random shallow-ish angle, either direction
	ballVX = (int16_t)(speed / 4 + rng() % (speed / 2));
	if (rng() & 1)
		ballVX = -ballVX;
	setState(STATE_PLAY);
}

static void drawPlayfield(void)
{
	tft.fillScreen(COLOR_BG);
	drawHud();
	drawAllBricks();
	// Mirrors the line under the HUD, and marks where to rest a finger:
	// everything below it is the control band, nothing is drawn there.
	tft.drawFastHLine(0, SCREEN_H - TOUCH_BAND_H, SCREEN_W, COLOR_LINE);
	ballDrawn = false;
	drawPaddle();
}

static void newGame(void)
{
	score = 0;
	lives = START_LIVES;
	level = 1;
	resetBricks();
	paddleX = (SCREEN_W - PADDLE_W) / 2;
	paddleTargetValid = false; // don't drift back to the last game's target
	drawPlayfield();
	startServe();
}

static void nextLevel(void)
{
	level++;
	resetBricks();
	eraseBall();
	drawPlayfield();
	startServe();
}

static void loseBall(void)
{
	eraseBall();
	lives--;
	drawLives();
	if (lives == 0) {
		drawTextCentered(110, "GAME OVER", 5, ST7789_RED);
		Serial.print("Game over, score ");
		Serial.println(score);
		setState(STATE_GAME_OVER);
	} else {
		startServe();
	}
}

// Advances the ball one frame: walls, bricks, paddle, and falling out.
static void stepBall(void)
{
	// --- horizontal move ---
	int16_t nx = ballX + ballVX;
	int16_t px = nx >> FP;
	int16_t py = ballY >> FP;
	if (px < 0) {
		nx = 0;
		ballVX = -ballVX;
	} else if (px > SCREEN_W - BALL_SIZE) {
		nx = (SCREEN_W - BALL_SIZE) << FP;
		ballVX = -ballVX;
	} else if (hitBricks(px, py)) {
		ballVX = -ballVX; // bounce back off the brick's side
		nx = ballX;
	}
	ballX = nx;

	// --- vertical move ---
	int16_t ny = ballY + ballVY;
	px = ballX >> FP;
	py = ny >> FP;
	if (py < HUD_H) {
		ny = HUD_H << FP;
		ballVY = -ballVY;
	} else if (hitBricks(px, py)) {
		ballVY = -ballVY;
		ny = ballY;
	} else if (ballVY > 0 &&
		   py + BALL_SIZE >= PADDLE_Y && py < PADDLE_Y + PADDLE_H &&
		   px + BALL_SIZE > paddleX && px < paddleX + PADDLE_W) {
		// Paddle hit: bounce angle set by where it landed on the paddle.
		// offset runs from -halfSpan (left tip) to +halfSpan (right tip).
		int16_t speed = levelSpeed();
		int16_t halfSpan = (PADDLE_W + BALL_SIZE) / 2;
		int16_t offset = (px + BALL_SIZE / 2) - (paddleX + PADDLE_W / 2);
		ballVX = (int16_t)((int32_t)offset * speed / halfSpan);
		if (ballVX == 0)
			ballVX = (rng() & 1) ? 2 : -2; // never perfectly vertical
		ballVY = -speed;
		ny = (PADDLE_Y - BALL_SIZE) << FP;
	}
	ballY = ny;

	// Past the paddle and into the touch band: the ball is lost. The
	// band is where the player's finger is, so a ball drawn in there
	// would be invisible -- and erasing it again would eat holes out of
	// the band's separator line. Losing it at the line keeps the band
	// empty, which is what makes it safe to cover. Checked before
	// drawBall() so the ball is never drawn below the line.
	if ((ballY >> FP) > SCREEN_H - TOUCH_BAND_H - BALL_SIZE) {
		loseBall();
		return;
	}
	drawBall();

	if (bricksLeft == 0)
		nextLevel();
}

// Reads the touch panel and moves the paddle toward the touched X.
// Returns true only when the panel actually read this frame, since the
// state machine uses that as "the screen is being touched".
static bool handleTouch(void)
{
	uint16_t tx, ty;
	bool got = touch.getPoint(tx, ty, SCREEN_W, SCREEN_H);

	if (got) {
		paddleTarget = (int16_t)tx - PADDLE_W / 2;
		paddleTargetValid = true;
	}

	// Carry on toward the last known target even on a frame the panel
	// gave nothing. This panel drops a sizeable share of readings while
	// a finger is sliding -- measured at 30-50% of polls on real
	// hardware, with the pressure channels reading as though nothing
	// were touching at all -- and returning early on each of those left
	// the paddle standing still for that frame, which is what made it
	// stutter rather than follow. Remembering the target turns a
	// dropped sample into lost smoothness instead of lost motion; only
	// a sustained loss parks the paddle. It does not make the panel
	// read any better, it just stops one bad sample from being visible.
	if (!paddleTargetValid)
		return got;

	int16_t target = paddleTarget;
	// Ease halfway toward the finger each frame: smooths the resistive
	// panel's jitter without making the paddle feel laggy.
	int16_t step = (target - paddleX) / 2;
	if (step == 0)
		step = (target > paddleX) ? 1 : (target < paddleX) ? -1 : 0;
	movePaddle(paddleX + step);
	return got;
}

static void showTitle(void)
{
	resetBricks();
	level = 1;
	score = 0;
	lives = START_LIVES;
	paddleX = (SCREEN_W - PADDLE_W) / 2;
	paddleTargetValid = false; // don't drift back to the last game's target
	drawPlayfield();
	drawTextCentered(130, "BREAKOUT", 6, ST7789_WHITE);
	drawTextCentered(180, "TOUCH TO START", 2, ST7789_CYAN);
	setState(STATE_TITLE);
}

// ---- Arduino entry points -------------------------------------------

void setup(void)
{
	Serial.begin(115200);

	tft.begin();
	// Portrait, 240x320. Rotation 0 writes straight through (MADCTL
	// 0x00), so the screen's bottom edge is the panel's own bottom --
	// which is the edge that was the right-hand side under rotation 1.
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
	// Run the TouchCalibration example if the paddle does not track
	// your finger closely enough near the edges.
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

	switch (state) {
	case STATE_TITLE:
		if (handleTouch()) {
			rngState ^= (uint16_t)micros();
			if (rngState == 0)
				rngState = 0xACE1;
			newGame();
		}
		break;

	case STATE_SERVE:
		handleTouch();
		placeBallOnPaddle(); // ball rides on the paddle until launch
		drawBall();
		if (now - stateSince >= SERVE_DELAY_MS)
			launchBall();
		break;

	case STATE_PLAY:
		handleTouch();
		stepBall();
		break;

	case STATE_GAME_OVER:
		// Short lockout so the touch that lost the last ball doesn't
		// immediately restart the game.
		if (now - stateSince > 1500 && handleTouch())
			showTitle();
		break;
	}
}
