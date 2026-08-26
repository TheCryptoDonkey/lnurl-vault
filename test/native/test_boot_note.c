/* The boot note, as it lands in hostgfx's framebuffer through the same
 * display.c the device draws with. These do not judge whether it is
 * beautiful -- `make preview` is for that -- they check that each pass
 * prints what it should, that a failed pass prints nothing, that the back
 * says what happened, and that none of it strays off the paper. */
#include <stdio.h>
#include <string.h>

#include "boot_screen.h"
#include "display.h"
#include "font5x7.h"
#include "guilloche.h"
#include "hostgfx.h"
#include "note_art.h"
#include "palette.h"
#include "unity_lite.h"

static const int PANEL_W[] = {240, 320};
static const int PANEL_H[] = {135, 170};
#define PANELS 2

static const uint8_t PUBKEY[32] = {0xA3, 0xF9, 0x2C, 0x1D, 0x7E, 0x40, 0x91, 0x0B,
                                   0x55, 0xC2, 0x18, 0x6A, 0xD9, 0x33, 0xF0, 0x27,
                                   0x8C, 0x61, 0xBE, 0x04, 0x4F, 0xA7, 0x12, 0xE8,
                                   0x39, 0x9D, 0x70, 0xC6, 0x0E, 0x83, 0x2B, 0x57};

static void panel(int i) {
    hostgfx_reset(PANEL_W[i], PANEL_H[i]);
    display_init();
}

static long count_in(uint16_t colour, int x0, int y0, int x1, int y1) {
    long n = 0;
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            if (hostgfx_pixel(x, y) == colour) {
                n++;
            }
        }
    }
    return n;
}

static uint16_t ink(void) {
    return display_state_ink(DISPLAY_STATE_IDLE);
}

static uint16_t dim(void) {
    return display_state_ink_dim(DISPLAY_STATE_IDLE);
}

static uint16_t ground(void) {
    return display_state_color(DISPLAY_STATE_IDLE);
}

/* What the band, bar and shutter are drawn in: the boot's royal blue. */
static uint16_t plate(void) {
    return PALETTE_ACCENT_BRAND;
}

/* A passed check's verdict colour (mint) and a failed one's (coral). */
static uint16_t ok_col(void) { return display_state_accent(DISPLAY_STATE_APPROVED); }
static uint16_t fail_col(void) { return display_state_accent(DISPLAY_STATE_DECLINED); }

/* What the note's plates -- border, rosette, crest -- are engraved in: the
 * royal blue the band wears. The NAME and serial are ink (off-white), so the
 * text can no longer be told from the plates by role alone; the tests locate
 * each by where it is. */
static uint16_t engraving(void) {
    return PALETTE_ACCENT_BRAND;
}

/* The wordmark sprite, the only INK-role sprite, so a test can find the
 * engraved name's rectangle without knowing the typeface's metrics. */
static const note_sprite_t *wordmark_of(const note_art_t *a) {
    for (int k = 0; k < a->nsprites; k++) {
        if (a->sprites[k].role == NOTE_ROLE_INK) {
            return &a->sprites[k];
        }
    }
    return NULL;
}

/* Where the serial is stamped, from the table: one copy, bottom-right, at
 * the readable floor. */
static void serial_rect(const note_art_t *a, int *x0, int *y0, int *x1, int *y1) {
    *x0 = a->serial_x;
    *y0 = a->serial_y;
    *x1 = a->serial_x + 9 * FONT5X7_ADVANCE * a->serial_scale;
    *y1 = a->serial_y + FONT5X7_HEIGHT * a->serial_scale;
}

/* The paper's rows, from the generated table -- which is also a check that
 * the table exists for both panels, because the fallback would draw a
 * plainer note without anyone noticing. */
static const note_art_t *art(int i) {
    const note_art_t *a = note_art_for(PANEL_W[i], PANEL_H[i]);
    UL_CHECK(a != NULL, "this panel has an engraving");
    return a;
}

static void test_the_sheet_is_laid_before_anything_is_printed(void) {
    for (int i = 0; i < PANELS; i++) {
        panel(i);
        boot_screen_begin("0.0.8", "t-display");
        const note_art_t *a = art(i);
        if (!a) {
            continue;
        }
        UL_CHECK(hostgfx_ink_pixels(HOSTGFX_UNPAINTED) == 0, "every pixel was painted");
        UL_CHECK(hostgfx_offscreen_pixels() == 0, "nothing drawn off the glass");
        /* Right-hand end of the band, clear of the version strip. */
        UL_CHECK(hostgfx_pixel(PANEL_W[i] - 2, 1) == plate(), "the band wears the boot's blue");
        const int nx1 = a->note_x + a->note_w, ny1 = a->note_y + a->note_h;
        UL_CHECK(count_in(ground(), a->note_x, a->note_y, nx1, ny1) == 0,
                 "the paper covers its whole rectangle");
        UL_CHECK(count_in(ink(), a->note_x, a->note_y, nx1, a->note_y + 2) == 0,
                 "no border along the top yet: nothing is printed");
        UL_CHECK(count_in(ink(), a->inner_x, a->inner_y, a->inner_x + a->inner_w,
                          a->inner_y + a->inner_h) > 0,
                 "but the name has been typed onto the sheet");
        UL_CHECK(count_in(ground(), 0, a->note_y, a->note_x, ny1) > 0,
                 "and desk shows round the edge of the sheet");
    }
}

static void test_the_bare_sheet_is_one_colour(void) {
    /* The sheet went out speckled once. The compositor dithered every
     * blend, and on the near-black paper that put magenta and green at the
     * lowest bit under all the type -- a faint grain in the PNGs, "the
     * purple" on glass, and everything unreadable over it. So: a patch of
     * bare paper, away from the name, is exactly one colour. */
    for (int i = 0; i < PANELS; i++) {
        panel(i);
        const note_art_t *a = art(i);
        if (!a) {
            continue;
        }
        boot_screen_begin("0.0.8", "t-display");
        /* The strip just inside the left border, the name's full height:
         * the name is centred and never reaches it. */
        const int x0 = a->inner_x, x1 = a->inner_x + 8;
        const int y0 = a->inner_y, y1 = a->inner_y + a->inner_h;
        const uint16_t paper = hostgfx_pixel(x0, y0);
        UL_CHECK(paper != ground() && paper != ink() && paper != plate(),
                 "the patch is paper, not something drawn on it");
        UL_CHECK(count_in(paper, x0, y0, x1, y1) == (long)(x1 - x0) * (y1 - y0),
                 "and every pixel of it is the same colour: no dither, no tint, no speckle");
    }
}

static void test_each_pass_prints_its_layer(void) {
    for (int i = 0; i < PANELS; i++) {
        panel(i);
        const note_art_t *a = art(i);
        if (!a) {
            continue;
        }
        const int nx1 = a->note_x + a->note_w;
        /* The rosette's own middle, well inside its box and clear of the
         * border, so only the rosette can put ink there. */
        const int rcx = a->rosette_cx, rcy = a->rosette_cy;
        const note_sprite_t *wm = wordmark_of(a);
        const note_sprite_t *promise = NULL; /* front content now, INK role like the name */
        for (int k = 0; k < a->nsprites; k++) {
            const note_sprite_t *sp = &a->sprites[k];
            if (sp->role == NOTE_ROLE_INK && sp != wm && sp->h < wm->h) {
                promise = sp;
            }
        }
        int sx0, sy0, sx1, sy1;
        serial_rect(a, &sx0, &sy0, &sx1, &sy1);

        boot_screen_begin("0.0.8", "t-display");
        boot_screen_step("STORAGE", true);
        UL_CHECK(count_in(engraving(), a->note_x, a->note_y, nx1, a->note_y + 2) > 0,
                 "the underprint puts the border along the top of the sheet");
        UL_CHECK(count_in(ink(), wm->x, wm->y, wm->x + wm->w, wm->y + wm->h) == 0,
                 "and sweeps the typed name away; no engraved name until the identity is up");
        const int rside = 2 * (a->rosette_r + 3);
        const int rbx = rcx - rside / 2, rby = rcy - rside / 2;
        UL_CHECK(count_in(engraving(), rbx, rby, rbx + rside, rby + rside) == 0, "and no rosette");
        UL_CHECK(count_in(ink(), sx0, sy0, sx1, sy1) == 0, "and no serial");

        boot_screen_identity(PUBKEY, sizeof(PUBKEY));
        boot_screen_step("IDENTITY", true);
        UL_CHECK(count_in(ink(), wm->x, wm->y, wm->x + wm->w, wm->y + wm->h) > 0,
                 "the intaglio prints the name in ink across the sheet");
        UL_CHECK(count_in(engraving(), rbx, rby, rbx + rside, rby + rside) > 0,
                 "and the rosette is engraved bottom-left");
        if (promise) {
            UL_CHECK(count_in(ink(), promise->x, promise->y, promise->x + promise->w,
                              promise->y + promise->h) > 0,
                     "and the promise under the name, where it fits");
        }
        UL_CHECK(count_in(ink(), sx0, sy0, sx1, sy1) == 0, "no serial until the numbering pass");

        boot_screen_step("LINK", true);
        UL_CHECK(count_in(ink(), sx0, sy0, sx1, sy1) > 0,
                 "the numbering stamps the serial bottom-right");
        UL_CHECK(a->serial_scale >= FONT5X7_MIN_READABLE_SCALE,
                 "at the readable floor: it is content, not texture");
        UL_CHECK(hostgfx_ink_pixels(HOSTGFX_UNPAINTED) == 0, "every pixel painted throughout");
        UL_CHECK(hostgfx_offscreen_pixels() == 0, "nothing off the glass");
        UL_CHECK(hostgfx_pixel(PANEL_W[i] - 1, PANEL_H[i] - 1) == plate(),
                 "three of three on the bar");
    }
}

static void test_a_failed_pass_is_not_printed(void) {
    for (int i = 0; i < PANELS; i++) {
        panel(i);
        const note_art_t *a = art(i);
        if (!a) {
            continue;
        }
        const int nx1 = a->note_x + a->note_w, ny1 = a->note_y + a->note_h;
        boot_screen_begin("0.0.8", "t-display");
        boot_screen_step("STORAGE", false);
        boot_screen_identity(PUBKEY, sizeof(PUBKEY));
        boot_screen_step("IDENTITY", true);
        boot_screen_step("LINK", true);
        UL_CHECK(count_in(engraving(), a->note_x, a->note_y, nx1, a->note_y + 2) == 0,
                 "no storage, no border");
        UL_CHECK(count_in(ink(), a->note_x, a->note_y, nx1, ny1) > 0,
                 "the passes that did come up still print");
        UL_CHECK(hostgfx_pixel(PANEL_W[i] - 1, PANEL_H[i] - 1) == plate(),
                 "the bar counts a failed pass as reached");
    }
}

static void test_a_failed_underprint_still_clears_the_typed_name(void) {
    /* Nothing rolls across to sweep it, so it has to simply go -- else the
     * intaglio prints the engraved name over the typed one. */
    for (int i = 0; i < PANELS; i++) {
        panel(i);
        const note_art_t *a = art(i);
        if (!a) {
            continue;
        }
        const int ix1 = a->inner_x + a->inner_w, iy1 = a->inner_y + a->inner_h;
        boot_screen_begin("0.0.8", "t-display");
        const long typed = count_in(ink(), a->inner_x, a->inner_y, ix1, iy1);
        UL_CHECK(typed > 0, "the name was typed");
        boot_screen_step("STORAGE", false);
        UL_CHECK(count_in(ink(), a->inner_x, a->inner_y, ix1, iy1) == 0,
                 "and is gone once storage has failed to print");
        boot_screen_identity(PUBKEY, sizeof(PUBKEY));
        boot_screen_step("IDENTITY", true);
        const long engraved = count_in(ink(), a->inner_x, a->inner_y, ix1, iy1);
        UL_CHECK(engraved > 0 && engraved < typed,
                 "the engraved name prints alone, and it is the smaller of the two");
    }
}

static void test_no_identity_means_no_rosette_and_no_serial(void) {
    for (int i = 0; i < PANELS; i++) {
        panel(i);
        const note_art_t *a = art(i);
        if (!a) {
            continue;
        }
        const int rcx = a->rosette_cx, rcy = a->rosette_cy;
        int sx0, sy0, sx1, sy1;
        serial_rect(a, &sx0, &sy0, &sx1, &sy1);
        boot_screen_begin("0.0.8", "t-display");
        boot_screen_step("STORAGE", true);
        boot_screen_step("IDENTITY", false);
        boot_screen_step("LINK", true);
        UL_CHECK(count_in(engraving(), rcx - 2, rcy - 2, rcx + 2, rcy + 2) == 0,
                 "a device with no identity boots without a rosette");
        UL_CHECK(count_in(ink(), sx0, sy0, sx1, sy1) == 0, "and without a serial");
        UL_CHECK(count_in(engraving(), a->note_x, a->note_y, a->note_x + a->note_w,
                          a->note_y + 2) > 0,
                 "the border it did earn is still there");
    }
}

static void test_the_back_is_the_self_check(void) {
    for (int i = 0; i < PANELS; i++) {
        panel(i);
        const note_art_t *a = art(i);
        if (!a) {
            continue;
        }
        const int ix1 = a->inner_x + a->inner_w, iy1 = a->inner_y + a->inner_h;
        boot_screen_begin("0.0.8", "t-display");
        boot_screen_step("STORAGE", true);
        boot_screen_identity(PUBKEY, sizeof(PUBKEY));
        boot_screen_step("IDENTITY", true);
        boot_screen_step("LINK", true);
        boot_screen_done();
        UL_CHECK(count_in(dim(), a->inner_x, a->inner_y, ix1, iy1) > 0,
                 "the back lists the check labels in the quiet weight");
        UL_CHECK(count_in(ok_col(), a->inner_x, a->inner_y, ix1, iy1) > 0,
                 "and every OK verdict in mint: the self-check looks live");
        UL_CHECK(count_in(fail_col(), a->inner_x, a->inner_y, ix1, iy1) == 0,
                 "with nothing in coral when all is well");
        UL_CHECK(count_in(engraving(), a->note_x, a->note_y, a->note_x + a->note_w,
                          a->note_y + 2) > 0,
                 "the border is on the back too");
        UL_CHECK(hostgfx_ink_pixels(HOSTGFX_UNPAINTED) == 0, "every pixel painted");
        UL_CHECK(hostgfx_offscreen_pixels() == 0, "nothing off the glass");
        UL_CHECK(hostgfx_pixel(PANEL_W[i] - 2, 1) == plate(),
                 "the flip left the band alone");

        panel(i);
        boot_screen_begin("0.0.8", "t-display");
        boot_screen_step("STORAGE", false);
        boot_screen_identity(PUBKEY, sizeof(PUBKEY));
        boot_screen_step("IDENTITY", true);
        boot_screen_step("LINK", true);
        boot_screen_done();
        UL_CHECK(count_in(fail_col(), a->inner_x, a->inner_y, ix1, iy1) > 0,
                 "a failed check shows its verdict in coral");
        UL_CHECK(count_in(engraving(), a->note_x, a->note_y, a->note_x + a->note_w,
                          a->note_y + 2) == 0,
                 "and a note with no underprint has no border on its back either");
    }
}

static void test_the_checks_fit_on_the_back(void) {
    /* Three lines at the readable size, with their gaps, inside the border:
     * on the classic panel that is 71 of 81 rows, which is the kind of
     * margin that silently becomes negative when someone widens the
     * gutter. */
    for (int i = 0; i < PANELS; i++) {
        const note_art_t *a = art(i);
        if (!a) {
            continue;
        }
        const int line = FONT5X7_HEIGHT * FONT5X7_MIN_READABLE_SCALE;
        const int block = 3 * line + 2 * (FONT5X7_CARD_GAP + 1);
        UL_CHECK(block <= a->inner_h, "three checks fit inside the border");
        /* The rosette's square and the serial's rectangle do not overlap,
         * and neither leaves the border. */
        int sx0, sy0, sx1, sy1;
        serial_rect(a, &sx0, &sy0, &sx1, &sy1);
        const int side = 2 * (a->rosette_r + 3);
        const int rx0 = a->rosette_cx - side / 2, ry0 = a->rosette_cy - side / 2;
        const int rx1 = rx0 + side, ry1 = ry0 + side;
        const bool apart = rx1 <= sx0 || sx1 <= rx0 || ry1 <= sy0 || sy1 <= ry0;
        UL_CHECK(apart, "the rosette stays clear of the serial");
        UL_CHECK(rx0 >= a->inner_x && ry0 >= a->inner_y && rx1 <= a->inner_x + a->inner_w &&
                     ry1 <= a->inner_y + a->inner_h,
                 "the rosette is inside the border");
        UL_CHECK(sx0 >= a->inner_x && sy0 >= a->inner_y && sx1 <= a->inner_x + a->inner_w &&
                     sy1 <= a->inner_y + a->inner_h,
                 "and so is the serial");
        UL_CHECK(side <= GUILLOCHE_MAX, "and the rosette fits its plane");
        /* Every engraved INK line -- the name and, where it fits, the
         * promise -- clears the readable floor for its kind. The name is the
         * tallest of them; the promise, if present, is shorter but still no
         * smaller than the serif floor the tool applies. Nothing is engraved
         * as texture. */
        const note_sprite_t *name = wordmark_of(a);
        UL_CHECK(name && name->h >= FONT5X7_HEIGHT * FONT5X7_MIN_READABLE_SCALE,
                 "the engraved name is no smaller than the 5x7 readable floor");
        for (int k = 0; k < a->nsprites; k++) {
            const note_sprite_t *sp = &a->sprites[k];
            if (sp->role == NOTE_ROLE_INK && sp != name) {
                UL_CHECK(sp->h >= 14, "a promise line is engraved only where it can be read");
            }
        }
    }
}

static void test_a_panel_with_no_engraving_still_gets_a_note(void) {
    /* No table for 200x120: the fallback sheet, rosette, serial and back.
     * Whatever a future board's panel turns out to be, the boot must not
     * become the one screen that draws nothing. */
    hostgfx_reset(200, 120);
    display_init();
    UL_CHECK(note_art_for(200, 120) == NULL, "no engraving for this size");
    boot_screen_begin("0.0.8", "other");
    boot_screen_step("STORAGE", true);
    boot_screen_identity(PUBKEY, sizeof(PUBKEY));
    boot_screen_step("IDENTITY", true);
    boot_screen_step("LINK", true);
    UL_CHECK(hostgfx_ink_pixels(HOSTGFX_UNPAINTED) == 0, "every pixel painted");
    UL_CHECK(hostgfx_offscreen_pixels() == 0, "nothing off the glass");
    UL_CHECK(hostgfx_ink_pixels(plate()) > 0, "the band is drawn in the boot's blue");
    UL_CHECK(hostgfx_ink_pixels(ink()) > 0, "the rosette, the name and the serial in ink");
    boot_screen_done();
    UL_CHECK(hostgfx_ink_pixels(dim()) > 0, "and the back lists the checks in the second weight");
    UL_CHECK(hostgfx_offscreen_pixels() == 0, "still nothing off the glass");
}

static void test_a_dead_panel_boots_quietly(void) {
    /* display_init() fails on a panel hostgfx cannot allocate rows for; the
     * boot must survive every call without a screen to draw on. */
    hostgfx_reset(1, 1);
    display_init();
    boot_screen_begin("0.0.8", "t-display");
    boot_screen_identity(PUBKEY, sizeof(PUBKEY));
    boot_screen_step("STORAGE", true);
    boot_screen_step("IDENTITY", true);
    boot_screen_step("LINK", true);
    boot_screen_done();
    UL_CHECK(hostgfx_offscreen_pixels() == 0, "nothing was drawn off a 1x1 panel");
}

void test_boot_note_run(void) {
    printf("-- boot note --\n");
    test_the_sheet_is_laid_before_anything_is_printed();
    test_the_bare_sheet_is_one_colour();
    test_each_pass_prints_its_layer();
    test_a_failed_pass_is_not_printed();
    test_a_failed_underprint_still_clears_the_typed_name();
    test_no_identity_means_no_rosette_and_no_serial();
    test_the_back_is_the_self_check();
    test_the_checks_fit_on_the_back();
    test_a_panel_with_no_engraving_still_gets_a_note();
    test_a_dead_panel_boots_quietly();
}
