# SDBitmapViewerDemo

A touch-driven photo viewer for the [Waveshare 2.8inch TFT Touch Shield](https://www.waveshare.com/2.8inch-tft-touch-shield.htm) (SKU: 10684). It shows 24-bit BMP files from the shield's microSD slot, driven by tap, swipe and long press, and falls back to a screen saver when left alone.

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

| Gesture | Effect |
| --- | --- |
| Tap | Next image |
| Swipe left | Next image |
| Swipe right | Previous image |
| Long press (hold still ~0.6s) | Back to the first image |
| Any touch during the screen saver | Returns to the image you were on |

A swipe has to travel at least 20px and be more horizontal than vertical; anything shorter counts as a tap. The long press fires while your finger is still down, without waiting for you to lift it, and only if the touch stays within 10px of where it started.

The touch that wakes the screen saver only wakes it — the tap or swipe it belongs to is discarded rather than also acted on, so you never overshoot by one image.

## `/PLAYLIST.JSN`

One file at the card's root holds everything:

```json
{
  "playlist":    ["/LOGO.BMP", "/PICS/SUNSET.BMP", "/PICS/MOUNTAIN.BMP"],
  "saver":       ["/PICS/SUNSET.BMP", "/PICS/MOUNTAIN.BMP"],
  "idle_ms":     60000,
  "interval_ms": 5000
}
```

| Member | Meaning | If left out |
| --- | --- | --- |
| `playlist` | Images to show, in this order | Every `.bmp` in the card's root, in FAT directory order |
| `saver` | Images the screen saver cycles through | The screen saver never starts |
| `idle_ms` | How long untouched before the saver starts | 60000 (60s) |
| `interval_ms` | How long each saver image stays up | 5000 (5s) |

Paths may point into subfolders and are shown in exactly the order written. Up to 32 entries, each up to 39 characters.

The extension is `.JSN`, not `.JSON`, because the bundled SD library only handles 8.3 filenames and cannot open a 4-character extension at all. The content is ordinary JSON.

A file containing nothing but a bare array is still accepted and read as the playlist, so cards written for the earlier one-list-per-file layout keep working untouched.

### Editing tips

- While trying the screen saver out, set `idle_ms` to something like `5000` so you are not waiting a minute each time.
- The parser looks up a member by name and reads what follows, so it is not a full JSON parser: a *path* that happened to be exactly `"playlist"`, `"saver"`, `"idle_ms"` or `"interval_ms"` would confuse it. Real paths never are.
- On macOS, copying with Finder leaves a `._name.bmp` metadata file beside each real one. The sketch filters these out on its own, but that is what they are if you go looking at the card.

## Serial output

At 115200 baud you get the list that was loaded, the settings in force, and a line per image with how long the draw took:

```
3 BMP file(s) from "playlist" in /PLAYLIST.JSN:
  /LOGO.BMP
  /PICS/SUNSET.BMP
  /PICS/MOUNTAIN.BMP
screen saver after 60000 ms, advancing every 5000 ms
drawing /LOGO.BMP
  top-down, 118 ms
tap
drawing /PICS/SUNSET.BMP
  bottom-up, 121 ms
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
| `SWIPE_WIPES_SIDEWAYS` | `false` | When true, a swipe wipes sideways in the direction your finger went instead of vertically |

Sideways wiping costs very different amounts on the two boards, which is what the timing in the serial output is for:

- **FRDM-MCXN947** decodes the whole picture into RAM first (150KB), so it reads the file in one sequential pass and every direction costs the same afterwards.
- **FRDM-MCXA153** has 24KB and cannot, so it reads from the card as it draws. Going sideways means walking the file against its grain — a seek in every row for each vertical band — and is markedly slower.

## Troubleshooting

**The screen goes solid red.** The sketch could not open or decode that file. Check the serial output: `unsupported BMP` means it is not 24-bit uncompressed, and any other failure means the path in `/PLAYLIST.JSN` does not match what is on the card. Remember the SD library only sees FAT 8.3 short names.

**`SD.begin() failed`.** The card is not being recognised. It must be FAT16 or FAT32 — cards over 32GB are SDXC and come exFAT-formatted, which this SD library cannot read at all. If the card was previously exFAT, use a full format rather than a quick one when converting it, since a quick format can leave stale partition data behind.

**The picture comes out white, drawing crawls, and the card then stops responding until you power-cycle.** Seen with cheap low-capacity cards — three of the same type behaved identically while better cards were fine on the same board and shield. Reading the whole file with no drawing in between succeeds every time; it only breaks once panel writes are interleaved with card reads. Nothing in this sketch compensates for it. Try a different card.

**The screen saver never starts.** It needs a `saver` member with at least one readable path. Check the serial output at startup: the settings line tells you the timeout actually in force.

**Everything is slow, on both boards.** Check the core version — mcx-arduino-core before 0.4.1 dropped the requested SPI clock silently and left the bus at whatever initialisation had set.

## Tuning

Constants at the top of the sketch, beyond the two transition settings above:

| Constant | Default | Meaning |
| --- | --- | --- |
| `MAX_BMP_FILES` | 32 | Entries a list may hold |
| `BMP_PATH_LEN` | 40 | Longest path, including the terminator |
| `SWIPE_THRESHOLD` | 20 | Pixels of travel before a drag counts as a swipe |
| `LONG_PRESS_MS` | 600 | How long a still touch becomes a long press |
| `LONG_PRESS_TOLERANCE` | 10 | Pixels a long press may drift and still count |
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
