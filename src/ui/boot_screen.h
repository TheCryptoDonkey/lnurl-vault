#ifndef LNURLVAULT_BOOT_SCREEN_H
#define LNURLVAULT_BOOT_SCREEN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* What the vault does with its screen for the first ten seconds.
 *
 * It used to draw three lines of grey text on the same grey the device rests
 * on, and be gone before anyone looked up. Then it typed its name onto a
 * field of the idle colour and listed what came up. Now it prints a bearer
 * note, because that is what it holds -- and it still types its name first.
 *
 * A shutter opens from the centre onto a blank sheet. The name types itself
 * onto it, big, in the device's own type, and holds. Then the note is
 * printed the way a real one is, in passes, and each pass is a boot step:
 * STORAGE lays the underprint (the border and the crest, sweeping the typed
 * name away as it rolls across), IDENTITY the intaglio (the rosette
 * engraved from the device's own key, then the name again in an engraved
 * hand, and the promise), LINK the numbering (the serial, which is the
 * key's first bytes). A pass whose subsystem failed is simply not printed,
 * so a vault with no storage boots with a bare sheet and a vault with no
 * identity has no rosette and no serial. Then the note turns over, and the
 * back is the self-check: the same three lines, OK or FAIL, at the readable
 * size, held long enough to read.
 *
 * The band and the bar wear royal blue -- the boot's own colour, the web
 * installer's -- and the note between them is drawn in ink, off-white on
 * the dark paper. Not a delicate coloured engraving: this panel muddies
 * low-contrast tone in its shadow region, so the note is read in black and
 * white and the blue is spent where it is bold. When ui_task paints the
 * idle card over the finished note the geometry stays put and the band
 * turns from the boot's blue to the state's teal.
 *
 * The fixed parts of the note are engraved offline by tools/engrave.py
 * (note_art.h); the rosette is drawn here from the identity (guilloche.h);
 * the rows are composed by note_scene.c and sent through display.c. So it
 * inherits the palette and the geometry rather than carrying a second copy
 * of either -- and test/native/preview.c renders every stage without a
 * board. */

/* The shutter, the blank sheet, and the name typed onto it. Call once,
 * straight after display_init(). `version` and `board` go on the band, where
 * the previous boot put them and where a bug report finds them. */
void boot_screen_begin(const char *version, const char *board);

/* The device's identity, for the rosette and the serial. Call before the
 * IDENTITY step and only when the key is actually being served: a device
 * that could not store its key has no identity to engrave, and must not boot
 * wearing one. Before boot_screen_begin() or without a panel, remembered
 * but not drawn. */
void boot_screen_identity(const uint8_t *pubkey, size_t len);

/* One printing pass, as that subsystem finishes coming up. `label` is short
 * and shouted -- STORAGE, IDENTITY, LINK -- and `ok` decides whether the pass
 * is printed. Either way the line is on the back of the note: a vault whose
 * storage did not come up still boots, still answers, and must say so before
 * anyone trusts a note to it.
 *
 * Three passes are expected. More are counted on the bar and listed on the
 * back if they fit; the ones past the bottom are dropped rather than drawn
 * over the border. */
void boot_screen_step(const char *label, bool ok);

/* Turns the note over, holds the self-check long enough to be read, then
 * returns. The caller starts ui_task afterwards, which draws the resting
 * card over it. */
void boot_screen_done(void);

#endif
