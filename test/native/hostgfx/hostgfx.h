#ifndef LNURLVAULT_HOSTGFX_H
#define LNURLVAULT_HOSTGFX_H

#include <stdbool.h>
#include <stdint.h>

/* A panel to run the firmware's own drawing code against, on a laptop.
 *
 * The shim headers beside this one satisfy display.c's four ESP dependencies
 * (esp_lcd, heap_caps, FreeRTOS, board) and point draw_bitmap at a
 * framebuffer, so the pixels a test inspects come from the same function that
 * draws on the device, at the same geometry and font. Stands in for ESP-IDF,
 * not for display.c -- a second implementation would drift.
 *
 * Used by test_card_render.c for assertions and preview.c for PNGs. */

/* A colour no screen ever draws, so "never painted" stays distinguishable
 * from "painted black" -- black being the ink on every note card. */
#define HOSTGFX_UNPAINTED 0xF81F /* magenta */

#define HOSTGFX_MAX_W 400
#define HOSTGFX_MAX_H 400

/* Sets what board_display_init() will report, and blanks the framebuffer.
 * Call before display_init(). Geometry larger than the maxima above is
 * clamped -- both real panels are well inside them. Also clears the swap
 * override below. */
void hostgfx_reset(int w, int h);

/* Makes board_display_init() report swap_bytes = `on`, so a test can
 * exercise display.c's byte-swap path (the classic panel needs it). Off by
 * default and cleared by hostgfx_reset. The swapped bytes are what lands in
 * the framebuffer, so a test reads back swap(value); what it is really
 * checking is that a fill comes out UNIFORM, which the in-place double-swap
 * bug broke. */
void hostgfx_set_swap(bool on);

int hostgfx_width(void);
int hostgfx_height(void);

/* RGB565 at (x, y); 0 for anything off-panel. */
uint16_t hostgfx_pixel(int x, int y);

/* A real panel swallows off-glass drawing silently, which is how a line comes
 * to be drawn and yet invisible. Here a test can insist it stays zero. */
long hostgfx_offscreen_pixels(void);

/* Text is one colour on a flat background, so this counts how much of it is
 * on screen -- the question a dropped line answers wrongly. */
long hostgfx_ink_pixels(uint16_t ink);

/* Topmost / bottommost inked row, or -1. */
int hostgfx_first_ink_row(uint16_t ink);
int hostgfx_last_ink_row(uint16_t ink);

/* Leftmost inked column, or -1. With the rightmost, this is how a test checks
 * centring without knowing any margins or glyph widths. */
int hostgfx_first_ink_col(uint16_t ink);

/* Rightmost inked column, or -1. Text clips at the edge rather than wrapping,
 * so how close a line got to running out of screen is worth asking. */
int hostgfx_last_ink_col(uint16_t ink);

/* Whether display.c has asked the board for the backlight. Starts on at
 * hostgfx_reset(); this is what lets a test tell "blanked to black and dark"
 * from "drew a black card and left the light on", which are the same
 * framebuffer and very different devices. */
bool hostgfx_backlight(void);

/* Draw, snapshot, draw the other thing, ask what moved: how a test locates a
 * band without knowing display.c's internal geometry. */
void hostgfx_snapshot(void);
int hostgfx_first_changed_row(void);

/* Pixels inside [x0, x1) x [y0, y1) that differ from the snapshot. For ink
 * that is anti-aliased rather than flat -- the boot note's engraving -- where
 * no pixel is exactly the colour asked for, and the question is whether
 * anything landed at all. */
long hostgfx_changed_pixels(int x0, int y0, int x1, int y1);

/* Called with the milliseconds asked for on every vTaskDelay the drawing
 * code makes, or NULL for none. The framebuffer at that moment is what the
 * glass would be showing for that long: a hook that writes it out gets an
 * animation, frame by frame, at the firmware's own timing. */
typedef void (*hostgfx_delay_hook_t)(unsigned int ms);
void hostgfx_set_delay_hook(hostgfx_delay_hook_t hook);

/* PNG at `path`, each pixel a zoom x zoom block. 0 on success.
 * Stored-deflate, so no libpng: a preview tool with a dependency is one
 * nobody runs. */
int hostgfx_write_png(const char *path, int zoom);

#endif
