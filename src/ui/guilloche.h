#ifndef LNURLVAULT_GUILLOCHE_H
#define LNURLVAULT_GUILLOCHE_H

#include <stddef.h>
#include <stdint.h>

/* The rosette on the boot note, engraved from the device's identity.
 *
 * A guilloche is the interlaced curve pattern on a banknote, and it was put
 * there to say which issuer printed it: the machine that ruled the curves
 * was the secret. This one is derived from the identity key (identity.h), so
 * every vault boots with a different rosette and the same vault boots with
 * the same one. The owner comes to know it the way they know their own note,
 * and a swapped device looks wrong before anything has been read. That is a
 * recognition cue, not a proof -- anyone with the public key can draw it --
 * and the challenge-response in identity.h is still what a wallet trusts.
 *
 * The curve is a hypotrochoid: a point fixed to a circle rolling inside a
 * larger one, the shape a Spirograph draws. Three integers and two fractions
 * out of the key pick the shape; nothing else about the key is recoverable
 * from the picture, and nothing needs to be.
 *
 * Rendered anti-aliased into a 4-bit coverage plane, the same form
 * note_art.h's engraved sprites take, so the compositor treats the one part
 * of the note drawn on the device exactly like the parts drawn offline.
 *
 * Portable: no ESP-IDF, single-precision maths only (both boards have an
 * FPU for exactly that and nothing wider). Exercised by
 * test/native/test_guilloche.c. */

/* The plane is square and this is its side. Sized for the larger panel's
 * rosette with its ring; guilloche_render() refuses anything bigger. */
#define GUILLOCHE_MAX 80
#define GUILLOCHE_PLANE_BYTES ((GUILLOCHE_MAX * GUILLOCHE_MAX + 1) / 2)

typedef struct {
    int turns;       /* how many times the small circle goes round: 3..6 */
    float reach;     /* the pen's distance from the small circle's centre,
                        as a fraction of the big radius */
    float rotation;  /* radians, so two devices with the same integers still
                        differ */
    float rotation2; /* for the inner curve */
} guilloche_params_t;

/* Reads the parameters out of the first bytes of `seed`. Any length works;
 * fewer than four bytes leaves the rest at their defaults. Deterministic. */
void guilloche_params_from_seed(const uint8_t *seed, size_t len, guilloche_params_t *out);

/* Draws the rosette into `plane`, a size x size 4-bit coverage plane packed
 * as note_art.h describes, clearing it first. `radius` is the outer curve's
 * reach in pixels, and with its ring the drawing needs 2 * (radius + 3)
 * pixels of side. Sizes over GUILLOCHE_MAX or radii that would not fit are
 * refused and the plane cleared.
 *
 * `permille` is how much of the pen's path has been drawn, 0..1000: the boot
 * screen calls this once per frame with an increasing value and the rosette
 * traces itself in. 1000 is the finished engraving. */
void guilloche_render(uint8_t *plane, int size, int radius, const guilloche_params_t *p,
                      int permille);

/* Coverage 0..15 at (x, y) of a plane laid out as above; 0 off the plane. */
uint8_t guilloche_coverage(const uint8_t *plane, int size, int x, int y);

#endif
