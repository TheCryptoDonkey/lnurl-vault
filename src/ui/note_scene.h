#ifndef LNURLVAULT_NOTE_SCENE_H
#define LNURLVAULT_NOTE_SCENE_H

#include <stdbool.h>
#include <stdint.h>

#include "note_art.h"

/* The boot note, composed a row at a time.
 *
 * display.c keeps no framebuffer -- it composes each row it draws into a
 * DMA buffer and sends it -- and this keeps to that. A scene is a description
 * of the note (what has been printed, what is mid-print, which side is up)
 * and note_scene_row() answers "what does panel row y look like right now".
 * The boot screen asks for whichever rows an animation frame changed and
 * hands each to display_draw_row(). Nothing is stored except the
 * description, so a wipe, a pen trace and a flip are all the same operation:
 * change the description, ask for the rows again.
 *
 * The parts: the paper, with a faint tint of the plate colour towards its
 * centre so it reads as a sheet rather than a rectangle; the engraved
 * sprites from note_art.h in three printing passes; the rosette from
 * guilloche.h; the serial, in the firmware's own 5x7; and on the back, the
 * self-check, in the same 5x7 at the readable size.
 *
 * Portable: no ESP-IDF, so test/native/test_boot_note.c inspects what it
 * composes and preview.c writes it out as PNGs through the same display.c
 * the device draws with. */

/* The widest panel a caller's row buffer must hold. Both real panels are
 * well inside it; hostgfx clamps to the same figure. */
#define NOTE_SCENE_MAX_W 400

#define NOTE_CHECKS_MAX 3
#define NOTE_LABEL_MAX 12
#define NOTE_SERIAL_MAX 12

/* The five colours a note is made of, resolved by the caller from palette.h
 * and display.c so this file carries no colour of its own. */
typedef struct {
    uint16_t ground; /* the desk the paper lies on */
    uint16_t paper;
    uint16_t accent; /* what the plates are engraved in: a grey, see palette.h */
    uint16_t ink;    /* the name, the promise, the serial */
    uint16_t dim;    /* a check's label, on the back only */
    uint16_t ok;     /* a passed check's verdict: a live, positive colour */
    uint16_t fail;   /* a failed check's verdict */
} note_inks_t;

typedef struct {
    char label[NOTE_LABEL_MAX];
    bool ok;
} note_check_t;

typedef struct {
    int w, h;
    const note_art_t *art; /* NULL: no engraving for this panel */
    note_inks_t inks;

    /* Where things are. Copied from the art, or derived when there is none. */
    int note_x, note_y, note_w, note_h;
    int inner_x, inner_y, inner_w, inner_h;
    int rosette_cx, rosette_cy, rosette_r;

    /* The tint: e = (x - cx)^2 * ka + (y - cy)^2 * kb in 16.16, looked up. */
    int tint_cx, tint_cy;
    uint32_t tint_ka, tint_kb;
    uint8_t tint_lut[256];

    /* Which passes are on the paper, as (1 << note_layer_t). */
    unsigned printed;
    /* The pass being printed now, wiping in from the left: everything left
     * of `wipe_edge` less `wipe_soft` is down, everything right of the edge
     * is not, and between them the ink fades. 0 for none. */
    note_layer_t wiping;
    int wipe_edge;
    int wipe_soft;

    /* The rosette's coverage plane (guilloche.h), or NULL for a device with
     * no identity to engrave. Drawn with the intaglio pass. */
    const uint8_t *rosette;
    int rosette_size;

    /* The serial, or "" for none. Printed by the numbering pass, at the
     * readable floor, once, bottom-right. */
    char serial[NOTE_SERIAL_MAX];
    int serial_scale, serial_x, serial_y;

    /* The name, typed onto the blank sheet before anything is printed, in
     * the firmware's own type at the largest size the sheet takes: two
     * lines, `splash_chars` characters of them so far, counted across both.
     * The underprint sweeps it away as it rolls across -- what is left of
     * it at any column is the inverse of how much of that pass is down. */
    char splash_top[NOTE_LABEL_MAX];
    char splash_bottom[NOTE_LABEL_MAX];
    int splash_scale;
    int splash_chars;

    /* The back of the note. `checks_shown` is how many of the `nchecks`
     * accumulated lines are actually drawn: the self-check reveals them one
     * at a time, so the flip lands on a blank back and each verdict arrives
     * on its own beat. */
    bool verso;
    note_check_t checks[NOTE_CHECKS_MAX];
    int nchecks;
    int checks_shown;
} note_scene_t;

/* Describes a blank sheet on a panel of w x h whose header band and progress
 * bar are `band` and `bar` rows tall. `art` may be NULL, and is treated as
 * NULL if it was engraved for a different panel or for a different band or
 * bar -- an engraving a pixel off the furniture it was drawn for is worse
 * than none, and that check is the whole defence against the generated
 * table and display.c drifting apart. */
void note_scene_begin(note_scene_t *s, int w, int h, int band, int bar, const note_art_t *art,
                      const note_inks_t *inks);

/* Composes panel row `y` into out[0..w). Rows above and below the paper are
 * the ground. Safe for any y; a caller animating the flip asks for rows the
 * paper would have and puts them somewhere else. */
void note_scene_row(const note_scene_t *s, int y, uint16_t *out);

/* Whether the pass is on the paper or going down right now. */
bool note_scene_layer_visible(const note_scene_t *s, note_layer_t layer);

/* The square the rosette occupies, for a caller repainting only that. */
void note_scene_rosette_box(const note_scene_t *s, int *x, int *y, int *size);

/* Sets the two lines of the name and picks their size: the largest scale at
 * which both fit the sheet's width AND the two of them, with air, fit its
 * height. Width alone picks the same scale on both panels -- five
 * characters is never what runs out first -- and then the second line
 * lands in the border on the shorter one. Nothing is typed yet. */
void note_scene_set_splash(note_scene_t *s, const char *top, const char *bottom);

/* The rows the name occupies, for a caller repainting only those as it
 * types. y1 is one past the last. */
void note_scene_splash_rows(const note_scene_t *s, int *y0, int *y1);

#endif
