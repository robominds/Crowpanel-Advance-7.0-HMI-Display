# Generated fonts

Real Montserrat at 144 px and 72 px, subset to only the characters the clock
view draws. Checked in rather than generated at build time: they are source as
far as this project is concerned, and requiring Node on every build to produce
a file that never changes would be a poor trade.

## Why they exist

LVGL's built-in Montserrat stops at 48 px. The clock view originally reached
its size by transform-scaling that 48 px font by three, which resamples a
bitmap that was never drawn at this size — visibly fuzzy next to natively
rendered text, which is what prompted this.

## Regenerating

LVGL ships the same source TTF it builds its own fonts from. From the project
root, with the library dependencies installed:

```sh
SCRIPTS=.pio/libdeps/advance_70/lvgl/scripts/built_in_font

npx -y lv_font_conv@1.5.3 --font "$SCRIPTS/Montserrat-Medium.ttf" \
  --size 144 --bpp 4 --format lvgl --no-compress --lv-include lvgl.h \
  --symbols "0123456789:.- " -o src/fonts/clock_font_144.c

npx -y lv_font_conv@1.5.3 --font "$SCRIPTS/Montserrat-Medium.ttf" \
  --size 72 --bpp 4 --format lvgl --no-compress --lv-include lvgl.h \
  --symbols "0123456789:.- APM" -o src/fonts/clock_font_72.c
```

Widen `--symbols` if the view ever needs a character it does not already carry.
A missing glyph does not fail the build; it simply does not draw.

| | 144 px | 72 px |
| --- | --- | --- |
| Used for | time digits, whole degrees | AM/PM, the tenth of a degree |
| Characters | `0-9 : . -` and space | the same, plus `A P M` |

Together they cost about 72 KB of flash, taking the firmware from roughly 50%
to 53% of its partition.
