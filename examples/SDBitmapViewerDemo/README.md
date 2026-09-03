# SDBitmapViewerDemo

English | [日本語](README.ja.md)

A touch-driven photo viewer for the [Waveshare 2.8inch TFT Touch Shield](https://www.waveshare.com/2.8inch-tft-touch-shield.htm) (SKU: 10684). It shows 24-bit BMP files from the shield's microSD slot, driven by tap, swipe and long press or by the board's own SW2/SW3 buttons, and falls back to a screen saver when left alone.

This is the elaborate sibling of [`SDBitmapViewer`](../SDBitmapViewer). That one is the portable example and runs on every board this library supports; this one targets **FRDM-MCXA153 and FRDM-MCXN947 only** and spends RAM freely to keep transitions quick. Building it for anything else stops with a clear `#error`.

## What you need

- The shield, seated on an FRDM-MCXA153 or FRDM-MCXN947
- [mcx-arduino-core](https://github.com/teddokano/mcx-arduino-core) **0.4.1 or newer** — earlier versions never actually applied the requested SPI clock, which makes everything crawl
- A microSD card, FAT16 or FAT32, 32GB or smaller
- The bundled Arduino `SD` library

BMP files must be **24-bit uncompressed** (BI_RGB) — what image editors produce by default as "24-bit bitmap". Images larger than 320x240 are clipped to the screen; smaller ones are drawn at the top-left corner.

## Quick start

1. Copy some 24-bit BMP files onto the card.
2. Copy [`PLAYLIST.JSN`](PLAYLIST.JSN) to the card's root and edit its paths to match your files.
3. Select your board, upload, and open the serial monitor at 115200 baud.

Without a `/PLAYLIST.JSN` the sketch simply shows every `.bmp` in the card's root instead, in whatever order the FAT directory happens to hold them.

## Controls

| Gesture | Landscape (default) | Portrait (`"portrait": true`) |
| --- | --- | --- |
| Touch upper half | — | Next image, wiping downwards |
| Touch lower half | — | Previous image, wiping upwards |
| Tap | Next image | (position decides, see above) |
| Swipe | Left = next, right = previous | Not used |
| Long press (hold still ~0.6s) | Opens the gallery | Opens the gallery |
| Touch, with the gallery up | Jumps to that thumbnail | Jumps to that thumbnail |
| Any touch during the screen saver | Returns to the image you were on | Returns to the image you were on |

In landscape a swipe has to travel at least 20px and be more horizontal than vertical; anything shorter counts as a tap.

The long press fires while your finger is still down, without waiting for you to lift it, and only if the touch stays within 10px of where it started.

**Portrait mode** is for holding the board on its side. Only the touch handling changes: a touch is read by *where* it landed rather than which way it moved, since flicking along the panel's long axis is awkward with the board turned. Touch the top half and the next picture wipes in downwards; touch the bottom half and the previous one wipes in upwards. Swipes are not read at all in this mode, and the wipe direction comes from the gesture rather than the alternating default. The panel itself is still driven in landscape, since the sketch has no way of knowing which way round you are holding the board, so the pictures are not rotated for you.

The touch that wakes the screen saver only wakes it — the tap or swipe it belongs to is discarded rather than also acted on, so you never overshoot by one image.

### The gallery

A long press replaces the picture with a 3x3 grid of the first nine images in the list, drawn as thumbnails. Touch one and it opens full screen; touch a cell with no picture behind it and nothing happens. Anything else that draws a picture — SW2/SW3, the screen saver starting — leaves the gallery as well.

Each cell is 106x80 with a 2px gutter, and a thumbnail is scaled to fit inside it without distorting, centred in whatever it does not fill. A cell whose file will not open or decode is filled red, the same as a failed full-screen draw.

Building the grid reads only the rows that land on a destination row and seeks straight past the rest, so at the 1/3 scale a 3x3 grid works out to, the whole thing costs about three full-screen draws rather than nine. The time it took is printed:

```
long press -> gallery
  gallery of 9 in 412 ms
```

In portrait mode the grid is still drawn the way the panel is driven, so with the board turned it reads down the columns rather than across the rows. Cells are picked by where you touch either way.

### The board's buttons

The FRDM board's own **SW2** and **SW3** step through the same list without touching the screen, in either orientation. SW2 goes back one image, SW3 forward one — and clicks are counted, so a double click moves two images, a triple three, and so on:

| Buttons | Result |
| --- | --- |
| SW3 once | Next image, wiping downwards |
| SW3 twice, quickly | Two images forward |
| SW2 three times, quickly | Three images back, wiping upwards |
| Either, during the screen saver | Returns to the image you were on |

Nothing in between is drawn: the sketch waits 400ms after the last click before moving, then makes the whole jump in one go. That is also what makes the counting work at all, since a single draw takes over 100ms and would otherwise still be running when the second click arrived.

Unlike a tap, the wipe direction is fixed rather than alternating with the index: SW3 always wipes down and SW2 always up, so the transition shows which way through the list you just moved.

The count is kept as a signed number of steps, so pressing both buttons within the same 400ms window subtracts one from the other; press each the same number of times and nothing moves. SW1 is the reset button and is left alone.

The buttons are read with `pinMode(SW2, INPUT_PULLUP)` and `digitalRead()`, using the `SW2` / `SW3` pin names mcx-arduino-core defines for both boards. Note that the pin one of them sits on doubles as the chip's ISP-mode input (SW2 on FRDM-MCXA153, SW3 on FRDM-MCXN947), so holding that button down while resetting the board can put it into the bootloader rather than running the sketch. Press them after the board is up.

## `/PLAYLIST.JSN`

One file at the card's root holds everything:

```json
{
  "playlist":    ["/LOGO.BMP", "/PICS/SUNSET.BMP", "/PICS/MOUNTAIN.BMP"],
  "saver":       ["/PICS/SUNSET.BMP", "/PICS/MOUNTAIN.BMP"],
  "idle_ms":     60000,
  "interval_ms": 5000,
  "portrait":    false,
  "reverse":     false
}
```

| Member | Meaning | If left out |
| --- | --- | --- |
| `playlist` | Images to show, in this order | Every `.bmp` in the card's root, in FAT directory order |
| `saver` | Images the screen saver cycles through | The screen saver never starts |
| `idle_ms` | How long untouched before the saver starts | 60000 (60s) |
| `interval_ms` | How long each saver image stays up | 5000 (5s) |
| `portrait` | `true` switches to touch-by-position for holding the board on its side | `false` |
| `reverse` | `true` shows every list back to front | `false` |

Paths may point into subfolders and are shown in exactly the order written. Up to 32 entries, each up to 39 characters.

The extension is `.JSN`, not `.JSON`, because the bundled SD library only handles 8.3 filenames and cannot open a 4-character extension at all. The content is ordinary JSON.

A file containing nothing but a bare array is still accepted and read as the playlist, so cards written for the earlier one-list-per-file layout keep working untouched.

### Editing tips

- A member that is missing, or whose value is malformed, simply leaves that setting at its default, so a card written without the newer members keeps behaving exactly as it did. The startup serial output tells you what is actually in force.
- While trying the screen saver out, set `idle_ms` to something like `5000` so you are not waiting a minute each time.
- `reverse` applies to the screen saver and the directory-scan fallback too, not just the playlist, so the setting means the same thing wherever the pictures came from.
- The parser looks up a member by name and reads what follows, so it is not a full JSON parser: a *path* that happened to be exactly one of the member names would confuse it. Real paths never are.
- On macOS, copying with Finder leaves a `._name.bmp` metadata file beside each real one. The sketch filters these out on its own, but that is what they are if you go looking at the card.

## Serial output

At 115200 baud you get the list that was loaded, the settings in force, and a line per image with how long the draw took:

```
3 BMP file(s) from "playlist" in /PLAYLIST.JSN:
  /LOGO.BMP
  /PICS/SUNSET.BMP
  /PICS/MOUNTAIN.BMP
landscape mode: swipe left / right
screen saver after 60000 ms, advancing every 5000 ms
list order: as written
SW2 = previous, SW3 = next; click n times to move n images
drawing /LOGO.BMP
  top-down, 118 ms
tap
drawing /PICS/SUNSET.BMP
  bottom-up, 121 ms
2 click(s) -> forward 2, wiping down
drawing /PICS/HARBOUR.BMP
  top-down, 119 ms
```

The direction shown is how that picture was painted on (see below), and the milliseconds are what the draw itself cost — handy when comparing wipe directions or boards.

## `/VIEWER.LOG`

Every image change is appended to `/VIEWER.LOG` on the same card, and each run starts with a restart marker:

```
0 ms  ---- restart ----
1243 ms  drew /LOGO.BMP
8120 ms  drew /PICS/SUNSET.BMP
13355 ms  FAILED /PICS/BROKEN.BMP
```

Timestamps are milliseconds since that run began, which is why the restart marker matters: they start over from zero every boot, and the marker is what keeps a file that outlives several runs readable.

If the card cannot be written the sketch says `could not append to /VIEWER.LOG` on the serial port rather than failing silently, and carries on showing pictures.

## Transitions

The old picture is never erased first — the new one simply overwrites it row by row, which avoids a black flash between images and makes the direction of the wipe the visible transition. A picture smaller than the screen therefore leaves whatever was around it still showing.

The direction alternates with the image index, so consecutive pictures wipe opposite ways. Two settings at the top of the sketch control this:

| Setting | Default | Effect |
| --- | --- | --- |
| `DRAW_BOTTOM_UP` | `false` | Which way even-numbered images go; odd ones take the other |
| `SWIPE_WIPES_SIDEWAYS` | `false` | When true, a swipe wipes the way your finger went rather than taking the alternating default — sideways in landscape, up or down in portrait |

SW2 and SW3 override both settings: they always wipe up and down respectively, whichever way the alternation happened to be going. A touch in portrait mode does the same.

Sideways wiping costs very different amounts on the two boards, which is what the timing in the serial output is for:

- **FRDM-MCXN947** decodes the whole picture into RAM first (150KB), so it reads the file in one sequential pass and every direction costs the same afterwards.
- **FRDM-MCXA153** has 24KB and cannot, so it reads from the card as it draws. Going sideways means walking the file against its grain — a seek in every row for each vertical band — and is markedly slower.

## Troubleshooting

**The screen goes solid red.** The sketch could not open or decode that file. Check the serial output: `unsupported BMP` means it is not 24-bit uncompressed, and any other failure means the path in `/PLAYLIST.JSN` does not match what is on the card. Remember the SD library only sees FAT 8.3 short names.

**`SD.begin() failed`.** The card is not being recognised. It must be FAT16 or FAT32 — cards over 32GB are SDXC and come exFAT-formatted, which this SD library cannot read at all. If the card was previously exFAT, use a full format rather than a quick one when converting it, since a quick format can leave stale partition data behind.

**The picture comes out white, drawing crawls, and the card then stops responding until you power-cycle.** Seen with cheap low-capacity cards — three of the same type behaved identically while better cards were fine on the same board and shield. Reading the whole file with no drawing in between succeeds every time; it only breaks once panel writes are interleaved with card reads. Nothing in this sketch compensates for it. Try a different card.

**SW2 and SW3 do nothing, or the board comes up unresponsive after a reset with one held down.** The pin one of the two sits on is also the chip's ISP-mode input — SW2 on FRDM-MCXA153, SW3 on FRDM-MCXN947 — so holding it while the board resets enters the bootloader instead of running the sketch. Release it and reset again. If they do nothing at any other time, check the serial output for the `SW2 = previous, SW3 = next` line: without it the sketch never got past `SD.begin()`.

**The screen saver never starts.** It needs a `saver` member with at least one readable path. Check the serial output at startup: the settings line tells you the timeout actually in force.

**Everything is slow, on both boards.** Check the core version — mcx-arduino-core before 0.4.1 dropped the requested SPI clock silently and left the bus at whatever initialisation had set.

## Tuning

Constants at the top of the sketch, beyond the two transition settings above:

| Constant | Default | Meaning |
| --- | --- | --- |
| `MAX_BMP_FILES` | 32 | Entries a list may hold |
| `BMP_PATH_LEN` | 40 | Longest path, including the terminator |
| `SWIPE_THRESHOLD` | 20 | Pixels of travel along the swipe axis before a drag counts as a swipe |
| `LONG_PRESS_MS` | 600 | How long a still touch becomes a long press |
| `LONG_PRESS_TOLERANCE` | 10 | Pixels a long press may drift and still count |
| `GALLERY_COLS` / `GALLERY_ROWS` | 3 / 3 | Grid the long press opens; the cell size follows from these |
| `GALLERY_GUTTER` | 2 | Pixels of background left between thumbnails |
| `MULTI_CLICK_MS` | 400 | How long after an SW2/SW3 click another one still joins the same count |
| `BUTTON_DEBOUNCE_MS` | 25 | How long an SW2/SW3 edge must settle to be believed |
| `CHUNK_PIXELS` | 64 | Pixels per SD read / LCD burst when drawing straight from the card |
| `BAND_PIXELS` | 64 | Width of a staged column band on the frame-buffer path |

`IDLE_TIMEOUT_MS` and `SAVER_INTERVAL_MS` are the fallbacks used when `/PLAYLIST.JSN` does not name `idle_ms` / `interval_ms`.

On FRDM-MCXA153 keep an eye on RAM if you raise `MAX_BMP_FILES` or `BMP_PATH_LEN`: they size the largest array in the sketch, and the linker script gives the stack only 2KB regardless of what the memory report says is free overall.

## Pin mapping

Fixed by the shield's PCB; nothing to wire.

| Signal | Pin |
| --- | --- |
| LCD_CS / LCD_DC / LCD_BL | D10 / D7 / D9 |
| TP_CS / TP_IRQ | D4 / D3 |
| SD_CS | D5 |
| SCLK / MOSI / MISO | D13 / D11 / D12 |
