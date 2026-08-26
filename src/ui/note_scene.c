/* See note_scene.h. */
#include "note_scene.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "font5x7.h"
#include "guilloche.h"

/* How far the tint gets at the centre of the paper, out of 255. Zero.
 *
 * It was a faint wash of the plate colour towards the middle of the sheet,
 * so a rectangle would read as paper. Rendering a gradient that shallow in
 * RGB565 needs dithering, and the dither is what was seen on glass as "the
 * purple": on a near-black ground the red and blue channels step together
 * and green steps on its own, so the sheet came out speckled magenta and
 * green at the lowest bit, which the panel's gamma made vivid. The tint is
 * not worth that. The machinery stays, at nought, for a panel that can
 * show it; the dither does not. */
#define TINT_MAX 0

/* The border, when there is no engraving to carry it: a plain inset. */
#define PLAIN_INSET 5

/* The serial is letterpress: the firmware's own type, at the readable
 * floor. The first cut stamped it at scale 2 in the dim weight, twice, on
 * the grounds that it was there to be found rather than seen; on glass it
 * could not be found either. Content is drawn at the size content is drawn
 * at, or not at all. */
#define SERIAL_SCALE FONT5X7_MIN_READABLE_SCALE
#define SERIAL_CHARS 9 /* "3D7E E0EB" */

/* --- colour ------------------------------------------------------------------ */

/* No dithering here, and that is a decision: see TINT_MAX. Anti-aliased
 * edges quantise perfectly well to RGB565 by rounding, and a sheet of one
 * colour must be one colour. */

static void unpack(uint16_t c, int *r, int *g, int *b) {
    *r = (int)(((c >> 11) & 0x1Fu) << 3);
    *g = (int)(((c >> 5) & 0x3Fu) << 2);
    *b = (int)((c & 0x1Fu) << 3);
    /* Replicate the high bits down so full is 255, not 248. */
    *r |= *r >> 5;
    *g |= *g >> 6;
    *b |= *b >> 5;
}

static int quantise(int v, int step_shift) {
    /* v is 0..255; to the nearest output step. */
    int q = (v + (1 << (step_shift - 1))) >> step_shift;
    const int max = 0xFF >> step_shift;
    return q > max ? max : q;
}

/* `col` over `base` at `a` out of 255. Exact at either end -- 0 returns the
 * base untouched and 255 the colour untouched -- so a test counting pixels
 * of a colour can trust what it counts. */
static uint16_t blend(uint16_t base, uint16_t col, int a, int x, int y) {
    if (a <= 0) {
        return base;
    }
    if (a >= 255) {
        return col;
    }
    int br, bg, bb, cr, cg, cb;
    unpack(base, &br, &bg, &bb);
    unpack(col, &cr, &cg, &cb);
    const int r = br + ((cr - br) * a) / 255;
    const int g = bg + ((cg - bg) * a) / 255;
    const int b = bb + ((cb - bb) * a) / 255;
    (void)x;
    (void)y;
    return (uint16_t)((quantise(r, 3) << 11) | (quantise(g, 2) << 5) | quantise(b, 3));
}

static uint16_t role_colour(const note_scene_t *s, note_role_t role) {
    switch (role) {
    case NOTE_ROLE_INK:
        return s->inks.ink;
    case NOTE_ROLE_DIM:
        return s->inks.dim;
    case NOTE_ROLE_ACCENT:
    default:
        return s->inks.accent;
    }
}

/* --- geometry ---------------------------------------------------------------- */

void note_scene_begin(note_scene_t *s, int w, int h, int band, int bar, const note_art_t *art,
                      const note_inks_t *inks) {
    memset(s, 0, sizeof(*s));
    s->w = w;
    s->h = h;
    if (inks) {
        s->inks = *inks;
    }
    if (art && (art->w != w || art->h != h || art->band != band || art->bar != bar)) {
        art = NULL;
    }
    s->art = art;
    if (art) {
        s->note_x = art->note_x;
        s->note_y = art->note_y;
        s->note_w = art->note_w;
        s->note_h = art->note_h;
        s->inner_x = art->inner_x;
        s->inner_y = art->inner_y;
        s->inner_w = art->inner_w;
        s->inner_h = art->inner_h;
        s->rosette_cx = art->rosette_cx;
        s->rosette_cy = art->rosette_cy;
        s->rosette_r = art->rosette_r;
        s->serial_scale = art->serial_scale;
        s->serial_x = art->serial_x;
        s->serial_y = art->serial_y;
    } else {
        /* The same proportions tools/engrave.py uses, for a panel it has
         * not seen: a sheet with a margin of desk, the rosette in the left
         * column under where the serial goes. */
        s->note_x = 3;
        s->note_y = band + 2;
        s->note_w = w - 6;
        s->note_h = h - bar - 2 - s->note_y;
        s->inner_x = s->note_x + PLAIN_INSET;
        s->inner_y = s->note_y + PLAIN_INSET;
        s->inner_w = s->note_w - 2 * PLAIN_INSET;
        s->inner_h = s->note_h - 2 * PLAIN_INSET;
        /* The serial bottom-right and the rosette bottom-left, in whatever
         * height the sheet has: with no engraving there is nothing above
         * them to make room for. */
        s->serial_scale = SERIAL_SCALE;
        const int sw = SERIAL_CHARS * FONT5X7_ADVANCE * SERIAL_SCALE - SERIAL_SCALE;
        const int sh = FONT5X7_HEIGHT * SERIAL_SCALE;
        s->serial_x = s->inner_x + s->inner_w - 4 - sw;
        s->serial_y = s->inner_y + s->inner_h - 2 - sh;
        int r = (s->inner_h - 4) / 2 - 3;
        if (r > GUILLOCHE_MAX / 2 - 3) {
            r = GUILLOCHE_MAX / 2 - 3;
        }
        if (r < 4) {
            r = 4;
        }
        s->rosette_r = r;
        s->rosette_cx = s->inner_x + 2 + r + 3;
        s->rosette_cy = s->inner_y + s->inner_h / 2;
    }
    if (s->note_w < 1 || s->note_h < 1) {
        s->note_w = 0;
        s->note_h = 0;
    }

    /* The tint's ellipse: the paper's centre, reaching 40% of its width and
     * 60% of its height before fading out entirely. In 16.16 so a row costs
     * two multiplies and a lookup per pixel rather than a square root. */
    s->tint_cx = s->note_x + s->note_w / 2;
    s->tint_cy = s->note_y + s->note_h / 2;
    const float a = (float)s->note_w * 0.40f;
    const float b = (float)s->note_h * 0.60f;
    s->tint_ka = a > 0 ? (uint32_t)(65536.0f / (a * a)) : 0;
    s->tint_kb = b > 0 ? (uint32_t)(65536.0f / (b * b)) : 0;
    for (int i = 0; i < 256; i++) {
        const float e = (float)i / 255.0f;
        const float t = 1.0f - e;
        s->tint_lut[i] = (uint8_t)((float)TINT_MAX * t * sqrtf(t) + 0.5f);
    }
}

bool note_scene_layer_visible(const note_scene_t *s, note_layer_t layer) {
    return (s->printed & (1u << layer)) != 0 || s->wiping == layer;
}

void note_scene_rosette_box(const note_scene_t *s, int *x, int *y, int *size) {
    const int side = s->rosette_size > 0 ? s->rosette_size : 2 * (s->rosette_r + 3);
    *x = s->rosette_cx - side / 2;
    *y = s->rosette_cy - side / 2;
    *size = side;
}

/* 0..256: how much of a pass's ink is down at column x. */
static int wipe_factor(const note_scene_t *s, note_layer_t layer, int x) {
    if (s->wiping != layer) {
        return 256;
    }
    if (x >= s->wipe_edge) {
        return 0;
    }
    if (s->wipe_soft <= 0 || x <= s->wipe_edge - s->wipe_soft) {
        return 256;
    }
    return ((s->wipe_edge - x) * 256) / s->wipe_soft;
}

/* --- the parts --------------------------------------------------------------- */

static void paper_row(const note_scene_t *s, int y, uint16_t *out) {
    const int dy = y - s->tint_cy;
    const uint32_t ey = (uint32_t)(dy * dy) * s->tint_kb;
    for (int x = s->note_x; x < s->note_x + s->note_w; x++) {
        const int dx = x - s->tint_cx;
        const uint32_t e = (uint32_t)(dx * dx) * s->tint_ka + ey;
        /* e is 16.16; 1.0 and beyond is outside the ellipse. */
        const uint32_t idx = e >> 8; /* 0..255 across 0.0..1.0 */
        const int a = idx < 256 ? s->tint_lut[idx] : 0;
        out[x] = blend(s->inks.paper, s->inks.accent, a, x, y);
    }
}

static void sprite_row(const note_scene_t *s, const note_sprite_t *sp, int y, uint16_t *out) {
    if (y < sp->y || y >= sp->y + sp->h) {
        return;
    }
    const int stride = (sp->w + 1) / 2;
    const uint8_t *row = sp->mask + (size_t)(y - sp->y) * (size_t)stride;
    const uint16_t col = role_colour(s, sp->role);
    for (int i = 0; i < sp->w; i++) {
        const int x = sp->x + i;
        if (x < 0 || x >= s->w) {
            continue;
        }
        const uint8_t b = row[i / 2];
        const int cov = (i & 1) ? (b & 0x0Fu) : (b >> 4);
        if (!cov) {
            continue;
        }
        const int a = (cov * 17 * wipe_factor(s, sp->layer, x)) >> 8;
        out[x] = blend(out[x], col, a, x, y);
    }
}

static void rosette_row(const note_scene_t *s, int y, uint16_t *out) {
    if (!s->rosette || s->rosette_size <= 0) {
        return;
    }
    int bx, by, side;
    note_scene_rosette_box(s, &bx, &by, &side);
    if (y < by || y >= by + side) {
        return;
    }
    for (int i = 0; i < side; i++) {
        const int x = bx + i;
        if (x < 0 || x >= s->w) {
            continue;
        }
        const int cov = guilloche_coverage(s->rosette, side, i, y - by);
        if (cov) {
            out[x] = blend(out[x], s->inks.accent, cov * 17, x, y);
        }
    }
}

/* One row of 5x7 text, opaque, clipped to the panel. The same cells
 * display_text() draws, so the serial and the checklist look like the rest
 * of the device's type rather than like a fourth face. */
static void text_row(const note_scene_t *s, int tx, int ty, int y, const char *text, int scale,
                     uint16_t col, int alpha, uint16_t *out) {
    if (y < ty || y >= ty + FONT5X7_HEIGHT * scale || alpha <= 0) {
        return;
    }
    const int gy = (y - ty) / scale;
    int x = tx;
    for (const char *c = text; *c; c++) {
        const uint8_t *glyph = font5x7_glyph(*c);
        for (int gx = 0; gx < FONT5X7_WIDTH; gx++) {
            if ((glyph[gx] >> gy) & 1) {
                for (int k = 0; k < scale; k++) {
                    const int px = x + gx * scale + k;
                    if (px >= 0 && px < s->w) {
                        out[px] = blend(out[px], col, alpha, px, y);
                    }
                }
            }
        }
        x += FONT5X7_ADVANCE * scale;
    }
}

static void serial_rows(const note_scene_t *s, int y, uint16_t *out) {
    if (!s->serial[0]) {
        return;
    }
    /* Once, bottom-right, where a note's numbering ends up, at the readable
     * floor and in full ink: the dim weight is a grey this panel muddies,
     * and a serial nobody can read is worse than a loud one. The numbering
     * pass has no soft edge -- a stamp is down or it is not. */
    text_row(s, s->serial_x, s->serial_y, y, s->serial, s->serial_scale, s->inks.ink, 255, out);
}

static void checks_rows(const note_scene_t *s, int y, uint16_t *out) {
    if (s->nchecks <= 0) {
        return;
    }
    const int scale = FONT5X7_MIN_READABLE_SCALE;
    const int line_h = FONT5X7_HEIGHT * scale;
    const int gap = FONT5X7_CARD_GAP + 1;
    const int block = s->nchecks * line_h + (s->nchecks - 1) * gap;
    int y0 = s->inner_y + (s->inner_h - block) / 2;
    if (y0 < s->inner_y) {
        y0 = s->inner_y;
    }
    const int lx = s->inner_x + 4;
    const int rx = s->inner_x + s->inner_w - 4;
    const int shown = s->checks_shown < s->nchecks ? s->checks_shown : s->nchecks;
    for (int i = 0; i < shown; i++) {
        const int ty = y0 + i * (line_h + gap);
        if (ty + line_h > s->inner_y + s->inner_h) {
            break; /* out of paper; boot_screen.h promises this is dropped, not drawn over */
        }
        const note_check_t *c = &s->checks[i];
        /* Label left in the quiet weight, verdict right in a live colour:
         * OK in the mint an approval wears, FAIL in the coral a refusal
         * does. The column of verdicts then reads as a row of green lights
         * with any red one obvious, which is what a self-check should look
         * like -- not a grey list. */
        text_row(s, lx, ty, y, c->label, scale, s->inks.dim, 255, out);
        const char *verdict = c->ok ? "OK" : "FAIL";
        text_row(s, rx - font5x7_text_width(verdict, scale), ty, y, verdict, scale,
                 c->ok ? s->inks.ok : s->inks.fail, 255, out);
    }
}

/* --- the name, typed --------------------------------------------------------- */

static int splash_block(const note_scene_t *s, int *line_h, int *gap) {
    *line_h = FONT5X7_HEIGHT * s->splash_scale;
    *gap = *line_h / 5;
    return 2 * *line_h + *gap;
}

void note_scene_set_splash(note_scene_t *s, const char *top, const char *bottom) {
    snprintf(s->splash_top, sizeof(s->splash_top), "%s", top ? top : "");
    snprintf(s->splash_bottom, sizeof(s->splash_bottom), "%s", bottom ? bottom : "");
    s->splash_chars = 0;
    int scale = font5x7_fit_scale(s->splash_top, s->inner_w, FONT5X7_MAX_SCALE);
    const int other = font5x7_fit_scale(s->splash_bottom, s->inner_w, FONT5X7_MAX_SCALE);
    if (other < scale) {
        scale = other;
    }
    /* Half a line of air demanded on top of the block itself. Without it
     * the fit is satisfied by a name that touches the border above and
     * below, which does not read as big -- it reads as too big for the
     * sheet. */
    while (scale > 1 && 2 * FONT5X7_HEIGHT * scale + (FONT5X7_HEIGHT * scale) / 5 +
                                (FONT5X7_HEIGHT * scale) / 2 >
                            s->inner_h) {
        scale--;
    }
    s->splash_scale = scale;
}

void note_scene_splash_rows(const note_scene_t *s, int *y0, int *y1) {
    int line_h, gap;
    const int block = splash_block(s, &line_h, &gap);
    *y0 = s->inner_y + (s->inner_h - block) / 2;
    *y1 = *y0 + block;
}

static void splash_rows(const note_scene_t *s, int y, uint16_t *out) {
    if (s->splash_chars <= 0 || s->splash_scale <= 0) {
        return;
    }
    int line_h, gap;
    splash_block(s, &line_h, &gap);
    int y0, y1;
    note_scene_splash_rows(s, &y0, &y1);
    if (y < y0 || y >= y1) {
        return;
    }
    /* Typed at a fixed left edge, so a line grows rightwards instead of
     * re-centring under itself on every character -- which looks like a bug
     * rather than like typing. */
    const int top_len = (int)strlen(s->splash_top);
    char buf[NOTE_LABEL_MAX];
    const char *lines[2] = {s->splash_top, s->splash_bottom};
    const int shown[2] = {s->splash_chars < top_len ? s->splash_chars : top_len,
                          s->splash_chars - top_len};
    for (int i = 0; i < 2; i++) {
        if (shown[i] <= 0) {
            continue;
        }
        const int full = font5x7_text_width(lines[i], s->splash_scale);
        const int x = s->inner_x + (s->inner_w - full) / 2;
        const int ty = y0 + i * (line_h + gap);
        snprintf(buf, sizeof(buf), "%.*s", shown[i], lines[i]);
        if (s->wiping == NOTE_LAYER_UNDERPRINT) {
            /* Swept away column by column as the plate rolls over it: each
             * character keeps what the pass has not yet reached. */
            const int cell = FONT5X7_ADVANCE * s->splash_scale;
            for (int c = 0; c < shown[i]; c++) {
                const char one[2] = {buf[c], '\0'};
                const int cx = x + c * cell;
                const int alpha = (255 * (256 - wipe_factor(s, NOTE_LAYER_UNDERPRINT, cx))) / 256;
                text_row(s, cx, ty, y, one, s->splash_scale, s->inks.ink, alpha, out);
            }
        } else {
            text_row(s, x, ty, y, buf, s->splash_scale, s->inks.ink, 255, out);
        }
    }
}

/* --- the row ---------------------------------------------------------------- */

void note_scene_row(const note_scene_t *s, int y, uint16_t *out) {
    for (int x = 0; x < s->w; x++) {
        out[x] = s->inks.ground;
    }
    if (y < s->note_y || y >= s->note_y + s->note_h || s->note_w <= 0) {
        return;
    }
    paper_row(s, y, out);

    const bool intaglio = note_scene_layer_visible(s, NOTE_LAYER_INTAGLIO);
    const bool numbering = note_scene_layer_visible(s, NOTE_LAYER_NUMBERING);

    if (s->art) {
        for (int i = 0; i < s->art->nsprites; i++) {
            const note_sprite_t *sp = &s->art->sprites[i];
            if (s->verso && !sp->two_sided) {
                continue;
            }
            if (!note_scene_layer_visible(s, sp->layer)) {
                continue;
            }
            sprite_row(s, sp, y, out);
        }
    }

    if (s->verso) {
        checks_rows(s, y, out);
        return;
    }
    splash_rows(s, y, out);
    if (intaglio) {
        rosette_row(s, y, out);
    }
    if (numbering) {
        serial_rows(s, y, out);
    }
}
