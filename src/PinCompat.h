/*
 * D0..D13 pin-number aliases, for cores that don't already define them.
 *
 * Some 32-bit Arduino cores (this shield's mcx-arduino-core, UNO R4
 * Minima's renesas_uno) define D0..D13 themselves so sketches can
 * write the more readable `D10` instead of a bare `10`. The classic
 * AVR core real UNO R3/Nano/Mega boards use does not define these at
 * all, which is why sketches using D10-style pin names failed to
 * build there.
 *
 * Gated on __AVR__ (defined by avr-gcc, the compiler every classic AVR
 * core uses) rather than `#ifndef D0`: on mcx-arduino-core, D0 is an
 * enum member, not a preprocessor macro, so `#ifndef` can't see it and
 * would incorrectly redefine D0 as a macro there too, which is exactly
 * the collision this header exists to avoid.
 */

#ifndef WAVESHARE_TFT_TOUCH_PIN_COMPAT_H
#define WAVESHARE_TFT_TOUCH_PIN_COMPAT_H

#include <Arduino.h>

#ifdef __AVR__

#define D0  0
#define D1  1
#define D2  2
#define D3  3
#define D4  4
#define D5  5
#define D6  6
#define D7  7
#define D8  8
#define D9  9
#define D10 10
#define D11 11
#define D12 12
#define D13 13

#endif // __AVR__

#endif // !WAVESHARE_TFT_TOUCH_PIN_COMPAT_H
