/* See guilloche.h. */
#include "guilloche.h"

#include <math.h>
#include <string.h>

/* How dark one pass of the pen is, out of 15. Under full coverage so that
 * where two curves cross the ink visibly builds up, which is what an
 * engraved crossing looks like -- and so that a single line reads as a
 * line, not as a solid edge. */
#define PEN_WEIGHT 11

/* Samples along each curve. Spacing matters more than count: the pen is
 * splatted, not stroked, so samples further apart than about half a pixel
 * leave the line beaded. The outer curve on the larger panel is about a
 * thousand pixels long. */
#define OUTER_SAMPLES 4000
#define INNER_SAMPLES 2400
#define RING_SAMPLES 900

static float pi_f(void) {
    return 3.14159265f;
}

void guilloche_params_from_seed(const uint8_t *seed, size_t len, guilloche_params_t *out) {
    out->turns = 5;
    out->reach = 0.55f;
    out->rotation = 0.0f;
    out->rotation2 = 0.0f;
    if (!seed) {
        return;
    }
    if (len > 0) {
        out->turns = 3 + (int)(seed[0] % 4u);
    }
    if (len > 1) {
        out->reach = 0.35f + 0.40f * ((float)seed[1] / 255.0f);
    }
    if (len > 2) {
        out->rotation = pi_f() * ((float)seed[2] / 255.0f);
    }
    if (len > 3) {
        out->rotation2 = pi_f() * ((float)seed[3] / 255.0f);
    }
}

/* --- the plane --------------------------------------------------------------- */

static uint8_t get4(const uint8_t *plane, int size, int x, int y) {
    const int stride = (size + 1) / 2;
    const uint8_t b = plane[y * stride + x / 2];
    return (x & 1) ? (b & 0x0Fu) : (b >> 4);
}

static void set4(uint8_t *plane, int size, int x, int y, uint8_t v) {
    const int stride = (size + 1) / 2;
    uint8_t *b = &plane[y * stride + x / 2];
    if (x & 1) {
        *b = (uint8_t)((*b & 0xF0u) | (v & 0x0Fu));
    } else {
        *b = (uint8_t)((*b & 0x0Fu) | (uint8_t)(v << 4));
    }
}

uint8_t guilloche_coverage(const uint8_t *plane, int size, int x, int y) {
    if (!plane || x < 0 || y < 0 || x >= size || y >= size) {
        return 0;
    }
    return get4(plane, size, x, y);
}

/* One curve is drawn with MAX into a scratch plane, so a pen that lingers
 * near a pixel does not darken it twice; then the scratch is ADDED into the
 * result, so different curves crossing do. Two planes rather than one is
 * what keeps the line weight even along a curve and heavy at the crossings,
 * which is the look. */
static uint8_t g_scratch[GUILLOCHE_PLANE_BYTES];

static void plot_max(uint8_t *plane, int size, int x, int y, float cov) {
    if (x < 0 || y < 0 || x >= size || y >= size) {
        return;
    }
    int v = (int)(cov * (float)PEN_WEIGHT + 0.5f);
    if (v <= 0) {
        return;
    }
    if (v > 15) {
        v = 15;
    }
    if (v > get4(plane, size, x, y)) {
        set4(plane, size, x, y, (uint8_t)v);
    }
}

/* A point with no size, spread over the four pixels it lies between by how
 * near it is to each. That IS the anti-aliasing: a sample landing dead on a
 * pixel centre inks that pixel alone; one landing between two inks each by
 * half. */
static void splat(uint8_t *plane, int size, float fx, float fy) {
    const float x0 = floorf(fx);
    const float y0 = floorf(fy);
    const float tx = fx - x0;
    const float ty = fy - y0;
    const int ix = (int)x0;
    const int iy = (int)y0;
    plot_max(plane, size, ix, iy, (1.0f - tx) * (1.0f - ty));
    plot_max(plane, size, ix + 1, iy, tx * (1.0f - ty));
    plot_max(plane, size, ix, iy + 1, (1.0f - tx) * ty);
    plot_max(plane, size, ix + 1, iy + 1, tx * ty);
}

static void add_scratch(uint8_t *plane, int size) {
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            const int v = get4(g_scratch, size, x, y);
            if (v) {
                int sum = get4(plane, size, x, y) + v;
                if (sum > 15) {
                    sum = 15;
                }
                set4(plane, size, x, y, (uint8_t)sum);
            }
        }
    }
}

/* x = (1 - r) cos t + d cos(((1 - r) / r) t)
 * y = (1 - r) sin t - d sin(((1 - r) / r) t)
 * on a unit circle, with r = 1 / turns so the curve closes after `turns`
 * revolutions, then scaled so its furthest reach is exactly R. Without the
 * scaling a wide `reach` puts the lobes past the ring drawn round them,
 * which the first render did. */
static void hypotrochoid(uint8_t *plane, int size, float cx, float cy, float R, int turns,
                         float reach, float rot, int samples, int limit) {
    const float r = 1.0f / (float)turns;
    const float k = (1.0f - r) / r;
    const float scale = R / ((1.0f - r) + reach);
    const float c = cosf(rot);
    const float s = sinf(rot);
    for (int i = 0; i <= limit && i <= samples; i++) {
        const float t = 2.0f * pi_f() * (float)turns * (float)i / (float)samples;
        const float x = scale * ((1.0f - r) * cosf(t) + reach * cosf(k * t));
        const float y = scale * ((1.0f - r) * sinf(t) - reach * sinf(k * t));
        splat(plane, size, cx + x * c - y * s, cy + x * s + y * c);
    }
}

static void ring(uint8_t *plane, int size, float cx, float cy, float R, int samples, int limit) {
    for (int i = 0; i <= limit && i <= samples; i++) {
        const float t = 2.0f * pi_f() * (float)i / (float)samples;
        splat(plane, size, cx + R * cosf(t), cy + R * sinf(t));
    }
}

void guilloche_render(uint8_t *plane, int size, int radius, const guilloche_params_t *p,
                      int permille) {
    if (!plane || size <= 0 || size > GUILLOCHE_MAX) {
        return;
    }
    const size_t bytes = (size_t)(((size + 1) / 2) * size);
    memset(plane, 0, bytes);
    if (!p || radius <= 0 || 2 * (radius + 3) > size) {
        return;
    }
    if (permille < 0) {
        permille = 0;
    }
    if (permille > 1000) {
        permille = 1000;
    }
    const float cx = (float)size / 2.0f;
    const float cy = (float)size / 2.0f;
    const float R = (float)radius;

    /* The pen draws the outer curve first, then the inner one, then the
     * ring round both, and `permille` is how far along that whole path it
     * has got. Each curve's share is proportional to its sample count so the
     * pen moves at one speed. */
    const int total = OUTER_SAMPLES + INNER_SAMPLES + RING_SAMPLES;
    const int drawn = (int)(((long)total * permille) / 1000);

    int limit = drawn;
    memset(g_scratch, 0, bytes);
    hypotrochoid(g_scratch, size, cx, cy, R, p->turns, p->reach, p->rotation, OUTER_SAMPLES,
                 limit);
    add_scratch(plane, size);

    limit = drawn - OUTER_SAMPLES;
    if (limit >= 0) {
        memset(g_scratch, 0, bytes);
        hypotrochoid(g_scratch, size, cx, cy, R * 0.70f, p->turns + 1, p->reach * 0.6f,
                     p->rotation2, INNER_SAMPLES, limit);
        add_scratch(plane, size);
    }

    limit = drawn - OUTER_SAMPLES - INNER_SAMPLES;
    if (limit >= 0) {
        memset(g_scratch, 0, bytes);
        ring(g_scratch, size, cx, cy, R + 2.5f, RING_SAMPLES, limit);
        /* The ring is a hairline: half the pen's weight. */
        for (size_t i = 0; i < bytes; i++) {
            const uint8_t b = g_scratch[i];
            g_scratch[i] = (uint8_t)((((b >> 4) + 1) / 2) << 4 | (((b & 0x0Fu) + 1) / 2));
        }
        add_scratch(plane, size);
    }
}
