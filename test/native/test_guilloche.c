/* The rosette: derived from the identity, drawn anti-aliased, and drawn the
 * same every time. What matters is that it is deterministic, that it stays
 * inside its plane, and that two devices do not share one. */
#include <string.h>

#include "guilloche.h"
#include "unity_lite.h"

#define SIZE 52
#define RADIUS 23

static const uint8_t SEED_A[8] = {0xA3, 0xF9, 0x2C, 0x1D, 0x7E, 0x40, 0x91, 0x0B};
static const uint8_t SEED_B[8] = {0x55, 0xC2, 0x18, 0x6A, 0xD9, 0x33, 0xF0, 0x27};

static long ink(const uint8_t *plane, int size) {
    long n = 0;
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            n += guilloche_coverage(plane, size, x, y);
        }
    }
    return n;
}

static void test_the_same_key_draws_the_same_rosette(void) {
    static uint8_t a[GUILLOCHE_PLANE_BYTES], b[GUILLOCHE_PLANE_BYTES];
    guilloche_params_t p;
    guilloche_params_from_seed(SEED_A, sizeof(SEED_A), &p);
    guilloche_render(a, SIZE, RADIUS, &p, 1000);
    guilloche_render(b, SIZE, RADIUS, &p, 1000);
    UL_CHECK(memcmp(a, b, (SIZE / 2) * SIZE) == 0, "two renders of one key are identical");
    UL_CHECK(ink(a, SIZE) > 0, "and there is a drawing");
}

static void test_two_keys_draw_two_rosettes(void) {
    static uint8_t a[GUILLOCHE_PLANE_BYTES], b[GUILLOCHE_PLANE_BYTES];
    guilloche_params_t pa, pb;
    guilloche_params_from_seed(SEED_A, sizeof(SEED_A), &pa);
    guilloche_params_from_seed(SEED_B, sizeof(SEED_B), &pb);
    guilloche_render(a, SIZE, RADIUS, &pa, 1000);
    guilloche_render(b, SIZE, RADIUS, &pb, 1000);
    UL_CHECK(memcmp(a, b, (SIZE / 2) * SIZE) != 0, "different keys, different rosettes");
}

static void test_the_parameters_stay_in_range(void) {
    /* Every byte value must give a shape the renderer can draw: the turns
     * bound the small circle, the reach bounds the pen. */
    for (int v = 0; v < 256; v++) {
        const uint8_t seed[4] = {(uint8_t)v, (uint8_t)(255 - v), (uint8_t)v, (uint8_t)v};
        guilloche_params_t p;
        guilloche_params_from_seed(seed, sizeof(seed), &p);
        UL_CHECK(p.turns >= 3 && p.turns <= 6, "turns in 3..6");
        UL_CHECK(p.reach >= 0.35f && p.reach <= 0.75f, "reach in 0.35..0.75");
        UL_CHECK(p.rotation >= 0.0f && p.rotation <= 3.15f, "rotation in 0..pi");
    }
    guilloche_params_t d;
    guilloche_params_from_seed(NULL, 0, &d);
    UL_CHECK(d.turns == 5, "no seed at all still gives a drawable default");
}

static void test_the_pen_stays_inside_its_ring(void) {
    static uint8_t plane[GUILLOCHE_PLANE_BYTES];
    guilloche_params_t p;
    guilloche_params_from_seed(SEED_A, sizeof(SEED_A), &p);
    guilloche_render(plane, SIZE, RADIUS, &p, 1000);
    const float cx = SIZE / 2.0f, cy = SIZE / 2.0f;
    long outside = 0;
    for (int y = 0; y < SIZE; y++) {
        for (int x = 0; x < SIZE; x++) {
            if (!guilloche_coverage(plane, SIZE, x, y)) {
                continue;
            }
            const float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy;
            /* The ring sits at RADIUS + 2.5; a splat reaches the next pixel
             * over, whose centre can be a further diagonal away. */
            if (dx * dx + dy * dy > (RADIUS + 4.5f) * (RADIUS + 4.5f)) {
                outside++;
            }
        }
    }
    UL_CHECK(outside == 0, "no ink lands outside the ring");
    UL_CHECK(guilloche_coverage(plane, SIZE, 0, 0) == 0, "the corner is clear");
    UL_CHECK(guilloche_coverage(plane, SIZE, -1, 5) == 0 &&
                 guilloche_coverage(plane, SIZE, 5, SIZE) == 0,
             "off the plane reads as nothing rather than as memory");
}

static void test_the_pen_traces_in(void) {
    static uint8_t plane[GUILLOCHE_PLANE_BYTES];
    guilloche_params_t p;
    guilloche_params_from_seed(SEED_A, sizeof(SEED_A), &p);
    guilloche_render(plane, SIZE, RADIUS, &p, 0);
    const long none = ink(plane, SIZE);
    guilloche_render(plane, SIZE, RADIUS, &p, 300);
    const long some = ink(plane, SIZE);
    guilloche_render(plane, SIZE, RADIUS, &p, 700);
    const long more = ink(plane, SIZE);
    guilloche_render(plane, SIZE, RADIUS, &p, 1000);
    const long all = ink(plane, SIZE);
    /* 0 is one splat: the pen has touched down. Not nothing, and not much. */
    UL_CHECK(none < 60, "at 0 the pen has barely touched the paper");
    UL_CHECK(some > none && more > some && all > more, "ink only accumulates as the pen goes");
    guilloche_render(plane, SIZE, RADIUS, &p, 5000);
    UL_CHECK(ink(plane, SIZE) == all, "past the end is the end");
}

static void test_a_plane_that_does_not_fit_is_refused(void) {
    static uint8_t plane[GUILLOCHE_PLANE_BYTES];
    guilloche_params_t p;
    guilloche_params_from_seed(SEED_A, sizeof(SEED_A), &p);
    memset(plane, 0xFF, sizeof(plane));
    guilloche_render(plane, GUILLOCHE_MAX + 1, 10, &p, 1000);
    UL_CHECK(plane[0] == 0xFF, "a plane wider than the buffer is not written at all");
    guilloche_render(plane, SIZE, SIZE, &p, 1000);
    UL_CHECK(ink(plane, SIZE) == 0, "a radius that would not fit clears the plane and draws nothing");
    guilloche_render(plane, SIZE, RADIUS, NULL, 1000);
    UL_CHECK(ink(plane, SIZE) == 0, "no parameters, no drawing");
}

static void test_crossings_are_darker_than_lines(void) {
    /* Two curves through one pixel add up; one curve's single pass does
     * not exceed its pen weight. That is the engraved look, and it is what
     * the scratch plane exists for. */
    static uint8_t plane[GUILLOCHE_PLANE_BYTES];
    guilloche_params_t p;
    guilloche_params_from_seed(SEED_A, sizeof(SEED_A), &p);
    guilloche_render(plane, SIZE, RADIUS, &p, 1000);
    int max = 0;
    for (int y = 0; y < SIZE; y++) {
        for (int x = 0; x < SIZE; x++) {
            const int v = guilloche_coverage(plane, SIZE, x, y);
            if (v > max) {
                max = v;
            }
        }
    }
    UL_CHECK(max > 11, "somewhere two curves cross and the ink builds up");
    UL_CHECK(max <= 15, "and never past full");
}

void test_guilloche_run(void) {
    printf("-- guilloche --\n");
    test_the_same_key_draws_the_same_rosette();
    test_two_keys_draw_two_rosettes();
    test_the_parameters_stay_in_range();
    test_the_pen_stays_inside_its_ring();
    test_the_pen_traces_in();
    test_a_plane_that_does_not_fit_is_refused();
    test_crossings_are_darker_than_lines();
}
