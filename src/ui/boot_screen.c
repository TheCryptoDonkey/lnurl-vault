/* See boot_screen.h. Drawing only, through display.c's primitives and
 * note_scene.c's rows, so the boot screen cannot drift from the palette or
 * the geometry the cards use.
 *
 * Every timing here is a delay this firmware did not used to take. That is
 * deliberate and it is the point -- but it is also latency before the device
 * answers a host, so the numbers are named and kept together rather than
 * scattered as magic values through the frames. The whole sequence is about
 * five and a half seconds -- snappier than the first slow cut, still slow
 * enough that each state of the note is seen. The render itself is a small
 * fraction of that; the rest is these holds, which is where to tune speed. */
#include "boot_screen.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "display.h"
#include "font5x7.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "guilloche.h"
#include "note_art.h"
#include "note_scene.h"
#include "palette.h"

/* The shutter: how long the field takes to open from the centre onto the
 * sheet, and in how many steps. Frames, not a duration, because a frame is
 * a stack of full-width blits and the panel is what sets the floor. */
#define SHUTTER_FRAMES 16
#define SHUTTER_FRAME_MS 22

/* One character of the name per beat. Slow enough to read as typing rather
 * than as a slow draw. */
#define TYPE_MS 80

/* The name, before the first pass sweeps it away. */
#define NAME_HOLD_MS 700

/* A pass rolling in from the left, like a plate going across. */
#define WIPE_FRAMES 14
#define WIPE_FRAME_MS 22

/* The rosette tracing itself in. */
#define TRACE_FRAMES 18
#define TRACE_FRAME_MS 22

/* Each pass, once it is down, before the next subsystem is waited on. Long
 * enough that each state of the note is SEEN, not just passed through: the
 * first cut of this ran at a quarter of these figures and the whole thing
 * read as a blur of things happening; the second at half, and it was still
 * "all too fast". Read at a desk, not watched as a film. */
#define STEP_HOLD_MS 350

/* The note turning over. */
#define FLIP_FRAMES 12
#define FLIP_FRAME_MS 24

/* Each self-check line, as it is revealed one at a time on the back: the
 * flip lands on a blank back, then STORAGE, IDENTITY, LINK arrive on their
 * own beat with the verdict in mint (or coral), which is where the green is
 * meant to be seen. */
#define CHECK_REVEAL_MS 380

/* The finished self-check, before ui_task paints over it. This is the one
 * screen of the sequence that exists to be READ, so it gets the longest
 * hold. */
#define VERSO_HOLD_MS 1400

/* Split rather than one string: "LNURL VAULT" across a 240px panel fits only
 * at the readable minimum, which is the size a note's label gets. The name
 * of the device should not be the smallest thing on its own boot screen. */
#define NAME_TOP "LNURL"
#define NAME_BOTTOM "VAULT"

/* How many passes the bar counts to. */
#define STEPS 3

/* Below the card minimum, deliberately.
 *
 * FONT5X7_MIN_READABLE_SCALE is a rule about note CONTENT -- an amount or a
 * label read at arm's length while deciding something. The identity strip is
 * neither: it is the firmware version and the board name, read once, up
 * close, by someone already looking for them, usually to put in a bug report.
 * At scale 3 the narrow panel holds thirteen characters and
 * "v0.0.7  t-display" is seventeen, so the rule would not make it more
 * readable -- it would cut the board name off, which is exactly the half a
 * bug report needs. */
#define STRIP_SCALE 2

static note_scene_t g_scene;
static uint16_t g_row[NOTE_SCENE_MAX_W];
static uint8_t g_rosette[GUILLOCHE_PLANE_BYTES];
static guilloche_params_t g_params;
static char g_serial[NOTE_SERIAL_MAX];
static bool g_have_identity;
static int g_steps;
static char g_strip[40];

/* --- the furniture ------------------------------------------------------- */

/* The band, the bar and the shutter are royal blue (PALETTE_ACCENT_BRAND):
 * the boot's own colour, the one the web installer wears. The idle card
 * this animates into has a teal band, so the colour changes when ui_task
 * paints over the finished note -- from the boot's blue to the state's teal
 * -- but the geometry does not, which is the point. The note between the
 * band and the bar is drawn in ink, not in this: see palette.h on why the
 * blue is furniture only. */
static uint16_t plate(void) {
    return PALETTE_ACCENT_BRAND;
}

/* Ink on the band and the bar is the card's own ground -- see palette.h on
 * why one value doing both jobs is what keeps them feeling like one device. */
static uint16_t band_ink(void) {
    return display_state_color(DISPLAY_STATE_IDLE);
}

static void draw_band(const char *text) {
    const int h = display_band_height();
    const int w = display_width();
    display_fill_rect(0, 0, w, h, plate());
    if (text && text[0]) {
        char fitted[sizeof(g_strip)];
        snprintf(fitted, sizeof(fitted), "%s", text);
        /* Cut to what the panel holds rather than letting display_text clip
         * mid-glyph: a board name sliced down the middle of a letter reads as
         * a different board name, not as a truncated one. */
        const int room = (w - 12) / (FONT5X7_ADVANCE * STRIP_SCALE);
        if (room > 0 && (int)strlen(fitted) > room) {
            fitted[room] = '\0';
        }
        const int th = FONT5X7_HEIGHT * STRIP_SCALE;
        display_text(6, (h - th) / 2, fitted, STRIP_SCALE, band_ink(), plate());
    }
}

/* The bar as a count, not a percentage: three passes, and the bar says how
 * many of them have been reached. It is the same bar the approval hold fills,
 * in the same place, which is the point -- by the time a prompt uses it the
 * shape is already familiar. */
static void draw_bar(int done, int total) {
    const int h = display_bar_height();
    const int y = display_height() - h;
    const int w = display_width();
    display_fill_rect(0, y, w, h, PALETTE_TRACK);
    if (done > 0 && total > 0) {
        int filled = (w * done) / total;
        if (filled > w) {
            filled = w;
        }
        if (filled > 0) {
            display_fill_rect(0, y, filled, h, plate());
        }
    }
}

/* --- the note, a row at a time ------------------------------------------- */

static void paint_rows(int y0, int y1) {
    const int w = display_width();
    for (int y = y0; y < y1; y++) {
        note_scene_row(&g_scene, y, g_row);
        display_draw_row(0, y, w, g_row);
    }
}

static void paint_note(void) {
    paint_rows(g_scene.note_y, g_scene.note_y + g_scene.note_h);
}

/* Ease-out, in parts per thousand: fast off the mark, settling at the end.
 * A linear wipe looks like a progress bar; this looks like a thing arriving. */
static int ease_out(int i, int n) {
    if (n <= 0 || i >= n) {
        return 1000;
    }
    const int left = n - i;
    return 1000 - (1000 * left * left) / (n * n);
}

/* The shutter.
 *
 * Starts as a full field of the royal blue and opens from the centre until
 * all that is left of it is the header band and the bar -- with the sheet
 * lying on the desk behind it. So the animation does not decorate the card
 * geometry, it ARRIVES at it, and what it arrives on is the paper the rest
 * of the boot prints. */
static void shutter(void) {
    const int w = display_width();
    const int h = display_height();
    const int top = display_band_height();
    const int bottom = h - display_bar_height();
    const int span = bottom - top;
    display_set_state(DISPLAY_STATE_IDLE);
    display_fill_rect(0, 0, w, h, plate());
    int shown = 0; /* rows of the middle revealed so far */
    for (int f = 1; f <= SHUTTER_FRAMES; f++) {
        int want = (span * ease_out(f, SHUTTER_FRAMES)) / 1000;
        if (want > span) {
            want = span;
        }
        if (want > shown) {
            /* Reveal outward from the centre: the rows newly uncovered above
             * and below what was already open. */
            const int centre = top + span / 2;
            const int old_top = centre - shown / 2;
            const int new_top = centre - want / 2;
            const int old_bottom = old_top + shown;
            const int new_bottom = new_top + want;
            paint_rows(new_top, old_top);
            paint_rows(old_bottom, new_bottom);
            shown = want;
        }
        vTaskDelay(pdMS_TO_TICKS(SHUTTER_FRAME_MS));
    }
    paint_rows(top, bottom);
}

/* A pass rolling across the sheet, left to right, with a soft leading edge. */
static void wipe_in(note_layer_t layer) {
    const int soft = g_scene.note_w / 5;
    g_scene.wiping = layer;
    g_scene.wipe_soft = soft;
    for (int f = 1; f <= WIPE_FRAMES; f++) {
        g_scene.wipe_edge =
            g_scene.note_x + ((g_scene.note_w + soft) * ease_out(f, WIPE_FRAMES)) / 1000;
        paint_note();
        vTaskDelay(pdMS_TO_TICKS(WIPE_FRAME_MS));
    }
    g_scene.wiping = 0;
    g_scene.printed |= 1u << layer;
    if (layer == NOTE_LAYER_UNDERPRINT) {
        /* Swept clean. Cleared BEFORE the settling paint below, or that paint
         * puts the whole name straight back. */
        g_scene.splash_chars = 0;
    }
    paint_note();
}

/* The rosette drawing itself, pen-plotter fashion. It belongs to the
 * intaglio pass, so while it traces the pass is "wiping" with its edge at
 * the left margin: the rosette shows, the type does not yet. */
static void trace_rosette(void) {
    int bx, by, side;
    g_scene.rosette = g_rosette;
    g_scene.rosette_size = 2 * (g_scene.rosette_r + 3);
    note_scene_rosette_box(&g_scene, &bx, &by, &side);
    g_scene.wiping = NOTE_LAYER_INTAGLIO;
    g_scene.wipe_edge = g_scene.note_x;
    g_scene.wipe_soft = 0;
    for (int f = 1; f <= TRACE_FRAMES; f++) {
        guilloche_render(g_rosette, side, g_scene.rosette_r, &g_params,
                         ease_out(f, TRACE_FRAMES));
        paint_rows(by, by + side);
        vTaskDelay(pdMS_TO_TICKS(TRACE_FRAME_MS));
    }
    guilloche_render(g_rosette, side, g_scene.rosette_r, &g_params, 1000);
    paint_rows(by, by + side);
}

/* Letterpress: down in one, no soft edge. Only the rows the serial lands on. */
static void stamp_serial(void) {
    snprintf(g_scene.serial, sizeof(g_scene.serial), "%s", g_serial);
    g_scene.printed |= 1u << NOTE_LAYER_NUMBERING;
    const int th = FONT5X7_HEIGHT * g_scene.serial_scale;
    paint_rows(g_scene.serial_y, g_scene.serial_y + th);
}

/* The note turning over about its long axis: squashed to a line and back
 * out the other side. Each frame maps the rows the sheet shows onto the
 * rows of the sheet at rest, so nothing is resampled -- a row is either one
 * of the note's own rows or it is desk. */
static void flip(void) {
    const int top = g_scene.note_y;
    const int h = g_scene.note_h;
    const float cy = (float)top + (float)h / 2.0f;
    const int w = display_width();
    for (int f = 1; f <= FLIP_FRAMES; f++) {
        const float s = cosf(3.14159265f * (float)f / (float)FLIP_FRAMES);
        if (s < 0.0f) {
            g_scene.verso = true;
        }
        const float mag = s < 0.0f ? -s : s;
        const float vis = mag * (float)h / 2.0f;
        for (int y = top; y < top + h; y++) {
            const float dy = (float)y + 0.5f - cy;
            const float ady = dy < 0.0f ? -dy : dy;
            if (mag < 0.01f || ady > vis) {
                for (int x = 0; x < w; x++) {
                    g_row[x] = g_scene.inks.ground;
                }
            } else {
                int src = (int)(cy + dy / mag);
                if (src < top) {
                    src = top;
                }
                if (src > top + h - 1) {
                    src = top + h - 1;
                }
                note_scene_row(&g_scene, src, g_row);
            }
            display_draw_row(0, y, w, g_row);
        }
        vTaskDelay(pdMS_TO_TICKS(FLIP_FRAME_MS));
    }
    g_scene.verso = true;
    paint_note();
}

/* The name, typed onto the blank sheet in the device's own type, one
 * character per beat. This was the whole of the previous boot's second act
 * and it is kept: before the note says anything about what came up, the
 * device says what it is, at a size that can be read from across the desk.
 * The engraved name on the finished note is the same words in a different
 * hand; this one is swept away by the first pass. */
static void type_name(void) {
    note_scene_set_splash(&g_scene, NAME_TOP, NAME_BOTTOM);
    int y0, y1;
    note_scene_splash_rows(&g_scene, &y0, &y1);
    const int total = (int)strlen(NAME_TOP) + (int)strlen(NAME_BOTTOM);
    for (int i = 1; i <= total; i++) {
        g_scene.splash_chars = i;
        paint_rows(y0, y1);
        vTaskDelay(pdMS_TO_TICKS(TYPE_MS));
    }
}

/* --- the sequence --------------------------------------------------------- */

void boot_screen_begin(const char *version, const char *board) {
    g_steps = 0;
    g_have_identity = false;
    g_serial[0] = '\0';
    g_strip[0] = '\0';
    if (version && board) {
        snprintf(g_strip, sizeof(g_strip), "v%s  %s", version, board);
    } else if (version) {
        snprintf(g_strip, sizeof(g_strip), "v%s", version);
    }

    if (!display_ready()) {
        return; /* a dead panel still boots -- it just has nothing to say */
    }

    const int w = display_width();
    const int h = display_height();
    const note_inks_t inks = {
        .ground = display_state_color(DISPLAY_STATE_IDLE),
        .paper = PALETTE_PAPER_NOTE,
        /* The plates -- border, rosette, crest -- in royal blue, the band's
         * own colour, so the note reads as one blue-and-white piece. The
         * NAME stays ink (below), off-white, so the one thing that must be
         * read is the highest-contrast thing on the sheet. This is only
         * legible because the panel's byte order is now correct
         * (board.h swap_bytes): an earlier cut engraved in ink precisely
         * because blue was reaching the glass as a muddy purple, which was
         * the swap, not the colour. */
        .accent = PALETTE_ACCENT_BRAND,
        .ink = display_state_ink(DISPLAY_STATE_IDLE),
        /* The quiet weight, for the self-check's labels on the back. The
         * verdicts beside them are not this -- see .ok/.fail. */
        .dim = display_state_ink_dim(DISPLAY_STATE_IDLE),
        .ok = display_state_accent(DISPLAY_STATE_APPROVED),
        .fail = display_state_accent(DISPLAY_STATE_DECLINED),
    };
    note_scene_begin(&g_scene, w, h, display_band_height(), display_bar_height(),
                     note_art_for(w, h), &inks);

    shutter();
    draw_band(g_strip);
    draw_bar(0, STEPS);
    type_name();
    vTaskDelay(pdMS_TO_TICKS(NAME_HOLD_MS));
}

void boot_screen_identity(const uint8_t *pubkey, size_t len) {
    if (!pubkey || len < 4) {
        return;
    }
    guilloche_params_from_seed(pubkey, len, &g_params);
    /* The serial is the key's first four bytes, as a note groups its digits.
     * The whole key is what a wallet pins; this is what a person can read off
     * the glass and compare with `identify`'s answer. */
    snprintf(g_serial, sizeof(g_serial), "%02X%02X %02X%02X", pubkey[0], pubkey[1], pubkey[2],
             pubkey[3]);
    g_have_identity = true;
}

void boot_screen_step(const char *label, bool ok) {
    if (!label || !label[0]) {
        return;
    }
    if (g_scene.nchecks < NOTE_CHECKS_MAX) {
        note_check_t *c = &g_scene.checks[g_scene.nchecks++];
        snprintf(c->label, sizeof(c->label), "%s", label);
        c->ok = ok;
    }
    if (!display_ready()) {
        return;
    }
    const int pass = ++g_steps;
    draw_bar(pass, STEPS);
    if (pass == 1 && !ok) {
        /* Nothing rolls across to sweep the name away, so it simply goes:
         * the intaglio must not print its own name over the typed one. */
        g_scene.splash_chars = 0;
        paint_note();
    }
    if (ok) {
        switch (pass) {
        case 1:
            wipe_in(NOTE_LAYER_UNDERPRINT);
            break;
        case 2:
            if (g_have_identity) {
                trace_rosette();
            }
            wipe_in(NOTE_LAYER_INTAGLIO);
            break;
        case 3:
            if (g_have_identity) {
                stamp_serial();
            }
            break;
        default:
            break; /* counted on the bar, listed on the back, nothing to print */
        }
    }
    vTaskDelay(pdMS_TO_TICKS(STEP_HOLD_MS));
}

void boot_screen_done(void) {
    if (!display_ready()) {
        return;
    }
    /* Turn over onto a blank back, then bring the checks up one at a time --
     * label, then its verdict in mint or coral -- so the self-check reads as
     * something happening, and the green lands where the eye is, not all at
     * once behind the flip. */
    g_scene.checks_shown = 0;
    flip();
    for (int i = 1; i <= g_scene.nchecks; i++) {
        g_scene.checks_shown = i;
        paint_note();
        vTaskDelay(pdMS_TO_TICKS(CHECK_REVEAL_MS));
    }
    vTaskDelay(pdMS_TO_TICKS(VERSO_HOLD_MS));
}
