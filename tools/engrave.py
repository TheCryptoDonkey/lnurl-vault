#!/usr/bin/env python3
"""Engraves the boot note: renders its fixed parts into src/ui/note_art.c.

    python3 tools/engrave.py                  # regenerate src/ui/note_art.c
    python3 tools/engrave.py --preview DIR    # and a PNG per panel, to look at

The boot screen is a bearer note printing itself (see src/ui/boot_screen.h).
Everything on it that is the same on every device -- the border, the crest,
the issuer's name, the promise -- is drawn HERE, once, at eight times the
panel's resolution and then sampled down to the panel's exact pixels, so the
curves are anti-aliased and the name is set in a real typeface. What the
device gets is a table of 4-bit coverage masks and where each one sits. It
never sees a font, a curve, or a floating point number for any of this.

Two things are deliberately NOT here, because they differ per device or per
boot: the rosette, which is derived from the device's identity key
(src/ui/guilloche.c), and the serial number and self-check, which are the
firmware's own 5x7 (src/ui/note_scene.c). The numbering on a real note is a
visibly cruder process than the intaglio around it, and that is the effect.

The typeface is Bodoni Moda, under the SIL Open Font License 1.1
(tools/fonts/OFL.txt). Rendered glyphs may be embedded and redistributed
under that licence; the OFL is what makes this a public repo's business at
all, which is why the Didot the first mock was drawn in is not what shipped.

Geometry the firmware also computes -- the header band and the progress bar
-- is mirrored from src/ui/display.c below and written INTO the table, and
note_scene.c refuses the table if the panel it is running on disagrees.
Drifting silently is the failure mode of every generated-asset pipeline, and
that check is the whole defence against it.

Needs Pillow and numpy. Deterministic: same inputs, same bytes out.
"""

import argparse
import math
import pathlib
import sys

try:
    import numpy as np
    from PIL import Image, ImageDraw, ImageFont
except ImportError as exc:  # pragma: no cover
    sys.exit(f"engrave.py needs Pillow and numpy: {exc}")

ROOT = pathlib.Path(__file__).resolve().parent.parent
FONT = ROOT / "tools" / "fonts" / "BodoniModa[opsz,wght].ttf"
OUT = ROOT / "src" / "ui" / "note_art.c"

# Supersampling factor. Eight is where a 0.5px hairline still lands as a
# smooth grey rather than a dotted line after the downsample.
S = 8

# The two panels src/board/ supports, as display.c reports them after the
# board has applied its orientation.
PANELS = [("t-display", 240, 135), ("t-display-s3", 320, 170)]

# --- mirrored from src/ui/display.c ----------------------------------------
FONT5X7_HEIGHT = 7
FONT5X7_MIN_READABLE_SCALE = 3
CARD_MARGIN = 6


def band_height(h):
    text = FONT5X7_HEIGHT * FONT5X7_MIN_READABLE_SCALE
    pad = max(3, CARD_MARGIN - 2)
    return min(text + 2 * pad, h // 3)


def bar_height(h):
    return max(8, h // 12)


# --- the note's own geometry ------------------------------------------------
# All of these are also read by src/ui/note_scene.c out of the generated
# table, so they are named once, here.
DESK = 3    # ground showing round the paper, each side
GUTTER = 4  # the hatched channel between the outer and inner rules
PAPER_INSET = 2  # rows of desk between the band/bar and the paper


FONT5X7_ADVANCE = 6
SERIAL_SCALE = 3  # the readable floor: the serial is content, not texture
SERIAL_CHARS = 9  # "3D7E E0EB"
ROW_GAP = 4

# The floor for engraved type. font5x7.h measured 21px of 5x7 as the least a
# person could read on this glass; a proper serif carries more shape per
# pixel and 14px of it read fine up close on the S3. Below that, a line is
# texture, and a note this size has no room for texture.
PROMISE_MIN_PX = 14


def layout(w, h):
    """Rows, top to bottom: the name across the whole sheet, the promise
    where it can be read, then the rosette, the crest and the serial along
    the bottom. The first cut put the name in a column between the rosette
    and the crest, which held it to 18px on the classic panel -- and the
    verdict on glass was that the text was unreadable. The name is the one
    thing on the note that has to be read, so it gets the width."""
    band, bar = band_height(h), bar_height(h)
    top = band + PAPER_INSET
    bottom = h - bar - PAPER_INSET
    ch = bottom - top
    inner_x, inner_y = DESK + GUTTER + 1, top + GUTTER + 1
    inner_w, inner_h = w - 2 * inner_x, bottom - inner_y - GUTTER - 1
    serial_w = SERIAL_CHARS * FONT5X7_ADVANCE * SERIAL_SCALE - SERIAL_SCALE
    serial_h = FONT5X7_HEIGHT * SERIAL_SCALE
    return dict(
        w=w, h=h, band=band, bar=bar,
        note_x=DESK, note_y=top, note_w=w - 2 * DESK, note_h=ch,
        inner_x=inner_x, inner_y=inner_y, inner_w=inner_w, inner_h=inner_h,
        serial_scale=SERIAL_SCALE,
        serial_x=inner_x + inner_w - 4 - serial_w,
        serial_y=inner_y + inner_h - 2 - serial_h,
        serial_w=serial_w, serial_h=serial_h,
        # filled in as the rows are engraved, top down:
        rosette_cx=0, rosette_cy=0, rosette_r=0,
        crest_cx=0, crest_cy=0, crest_w=0, crest_h=0,
    )


def bottom_row(L, row_top):
    """Places the rosette, the serial and (where there is room) the crest in
    whatever is left under the type. Mutates L."""
    row_bottom = L["inner_y"] + L["inner_h"] - 1
    row_h = row_bottom - row_top
    R = min((row_h - 2) // 2 - 3, round(L["note_h"] * 0.25))
    L["rosette_r"] = R
    L["rosette_cx"] = L["inner_x"] + 2 + R + 3
    L["rosette_cy"] = row_top + row_h // 2
    gap_l = L["rosette_cx"] + R + 3 + 6
    gap_r = L["serial_x"] - 6
    if gap_r - gap_l >= 44:
        L["crest_h"] = round(row_h * 0.42)
        L["crest_w"] = round(L["crest_h"] * 0.65)
        L["crest_cx"] = (gap_l + gap_r) // 2
        L["crest_cy"] = L["rosette_cy"]


# --- drawing helpers, all at S x --------------------------------------------
def canvas(w, h):
    img = Image.new("L", (w * S, h * S), 0)
    return img, ImageDraw.Draw(img)


def px(v):
    return int(round(v * S))


def rose(cx, cy, R, k, inner, phase, squash, steps=1600):
    pts = []
    for i in range(steps + 1):
        t = 2 * math.pi * i / steps
        rr = R * (inner + (1 - inner) * math.cos(k * t + phase))
        pts.append((px(cx + rr * math.cos(t) * squash), px(cy + rr * math.sin(t))))
    return pts


def font_at(size_px, wght=600, opsz=9):
    f = ImageFont.truetype(str(FONT), max(1, int(round(size_px * S))))
    # Weight and optical size, in the order the font declares its axes.
    # Bodoni's hairlines vanish below a pixel; a small optical size and a
    # semi-bold keep them, which is what "designed for text sizes" means.
    f.set_variation_by_axes([wght, opsz])
    return f


def tracked_width(draw, text, f, track):
    return sum(draw.textlength(c, font=f) for c in text) + track * S * (len(text) - 1)


def draw_tracked(draw, x, y, text, f, track, fill=255):
    for c in text:
        draw.text((x, y), c, font=f, fill=fill)
        x += draw.textlength(c, font=f) + track * S


def downsample(img_L, w, h):
    # Box, not Lanczos: each panel pixel is the plain average of its 8x8
    # block, which is what coverage means. Lanczos rings, and a solid rule
    # came out a shade under full -- which the tests noticed before anyone
    # would have.
    return img_L.resize((w, h), Image.BOX)


def crop_to_ink(small):
    """Returns (x, y, w, h, 4-bit rows) for the non-zero bbox of an L image."""
    a = np.asarray(small, dtype=np.uint8)
    ys, xs = np.nonzero(a)
    if len(ys) == 0:
        return None
    y0, y1, x0, x1 = ys.min(), ys.max() + 1, xs.min(), xs.max() + 1
    return int(x0), int(y0), int(x1 - x0), int(y1 - y0), quantise(a[y0:y1, x0:x1])


def quantise(a):
    # 0..255 coverage to 0..15, rounding, so a faint hairline that lands at
    # 8/255 stays a 1 rather than dropping out entirely.
    return ((a.astype(np.uint16) * 15 + 127) // 255).astype(np.uint8)


def pack4(rows):
    """Two pixels per byte, high nibble on the left, rows padded to a byte."""
    h, w = rows.shape
    out = bytearray()
    for y in range(h):
        for x in range(0, w, 2):
            left = int(rows[y, x])
            right = int(rows[y, x + 1]) if x + 1 < w else 0
            out.append((left << 4) | right)
    return bytes(out)


# --- the sprites ------------------------------------------------------------
def engrave_frame(L):
    """The border: a solid outer rule, a hatched gutter, a faint inner rule.
    Emitted as four strips, since the middle of the note is empty and a
    coverage mask of empty costs the same as one of ink."""
    w, h = L["w"], L["h"]
    img, d = canvas(w, h)
    x0, y0 = L["note_x"], L["note_y"]
    x1, y1 = x0 + L["note_w"], y0 + L["note_h"]
    d.rectangle([px(x0), px(y0), px(x1) - 1, px(y1) - 1], outline=255, width=px(1.0))
    g = GUTTER
    d.rectangle([px(x0 + g), px(y0 + g), px(x1 - g) - 1, px(y1 - g) - 1], outline=120,
                width=px(0.5))
    hatch, hw = 150, px(0.5)
    for x in range(x0 + 1, x1 - 1, 3):
        d.line([(px(x), px(y0 + g - 1)), (px(x + 2), px(y0 + 1))], fill=hatch, width=hw)
        d.line([(px(x), px(y1 - 1)), (px(x + 2), px(y1 - g + 1))], fill=hatch, width=hw)
    for y in range(y0 + 1, y1 - 1, 3):
        d.line([(px(x0 + 1), px(y + 2)), (px(x0 + g - 1), px(y))], fill=hatch, width=hw)
        d.line([(px(x1 - g + 1), px(y + 2)), (px(x1 - 1), px(y))], fill=hatch, width=hw)
    small = downsample(img, w, h)
    a = np.asarray(small, dtype=np.uint8)
    strips = []
    t = g + 1  # rows the top and bottom strips take
    def strip(sname, x, y, block):
        return (sname, x, y, int(block.shape[1]), int(block.shape[0]), quantise(block))

    strips.append(strip("frame_top", x0, y0, a[y0:y0 + t, x0:x1]))
    strips.append(strip("frame_bottom", x0, y1 - t, a[y1 - t:y1, x0:x1]))
    strips.append(strip("frame_left", x0, y0 + t, a[y0 + t:y1 - t, x0:x0 + t]))
    strips.append(strip("frame_right", x1 - t, y0 + t, a[y0 + t:y1 - t, x1 - t:x1]))
    return strips


def engrave_crest(L):
    """The issuer's crest: an oval of nested roses. Fixed, not seeded -- this
    is the device saying what it is, where the rosette says which one. Only
    where the bottom row has room for it beside the rosette and the serial,
    which the classic panel does not."""
    if L["crest_h"] <= 0:
        return []
    w, h = L["w"], L["h"]
    img, d = canvas(w, h)
    cx, cy, ow, oh = L["crest_cx"], L["crest_cy"], L["crest_w"], L["crest_h"]
    d.ellipse([px(cx - ow), px(cy - oh), px(cx + ow), px(cy + oh)], outline=220, width=px(0.6))
    d.ellipse([px(cx - ow - 2), px(cy - oh - 2), px(cx + ow + 2), px(cy + oh + 2)], outline=90,
              width=px(0.4))
    squash = ow / oh
    for i, (sc, al, ph) in enumerate([(0.9, 150, 0.0), (0.7, 120, 0.9), (0.5, 95, 1.7)]):
        d.line(rose(cx, cy, oh * sc, 7, 0.55 + 0.1 * i, ph, squash), fill=al, width=px(0.42),
               joint="curve")
    r = crop_to_ink(downsample(img, w, h))
    return [("crest",) + r]


def engrave_wordmark(L):
    """LNURL VAULT across the sheet, in the heaviest cut that still reads as
    Bodoni, at the largest size the width takes. Placed by its ink box, so
    both panels put it at the same distance below the border regardless of
    the face's own metrics. Returns the sprite and where its ink ends."""
    w, h = L["w"], L["h"]
    avail = (L["inner_w"] - 12) * S
    text, track = "LNURL VAULT", 1.0
    size = L["note_h"] * 0.42
    img, d = canvas(w, h)
    f = font_at(size, wght=700, opsz=7)
    while tracked_width(d, text, f, track) > avail and size > 6:
        size -= 0.25
        f = font_at(size, wght=700, opsz=7)
    tw = tracked_width(d, text, f, track)
    cx = L["inner_x"] + L["inner_w"] / 2
    x = cx * S - tw / 2
    probe, pd = canvas(w, h)
    draw_tracked(pd, x, 0, text, f, track)
    bb = probe.getbbox()
    y = (L["inner_y"] + 3) * S - bb[1]
    draw_tracked(d, x, y, text, f, track)
    r = crop_to_ink(downsample(img, w, h))
    return [("wordmark",) + r], (y + bb[3]) / S


def engrave_promise(L, below):
    """The Bank of England's wording, because that is literally what a bearer
    note is -- but only where it lands at PROMISE_MIN_PX or better across the
    sheet. On the classic panel it cannot, and a line nobody can read
    conveys nothing (font5x7.h), so that panel goes without. Returns the
    sprite (or nothing) and where its ink ends."""
    w, h = L["w"], L["h"]
    avail = (L["inner_w"] - 12) * S
    track = 0.6
    for text in ("I PROMISE TO PAY THE BEARER ON DEMAND", "PAY THE BEARER ON DEMAND"):
        size = L["note_h"] * 0.16
        img, d = canvas(w, h)
        f = font_at(size, wght=600, opsz=8)
        while tracked_width(d, text, f, track) > avail and size > PROMISE_MIN_PX:
            size -= 0.25
            f = font_at(size, wght=600, opsz=8)
        if tracked_width(d, text, f, track) <= avail:
            break
    else:
        return [], below
    tw = tracked_width(d, text, f, track)
    cx = L["inner_x"] + L["inner_w"] / 2
    x = cx * S - tw / 2
    probe, pd = canvas(w, h)
    draw_tracked(pd, x, 0, text, f, track)
    bb = probe.getbbox()
    y = (below + 3.5) * S - bb[1]
    draw_tracked(d, x, y, text, f, track)
    r = crop_to_ink(downsample(img, w, h))
    return [("promise",) + r], (y + bb[3]) / S


LAYER = {"frame": 1, "crest": 1, "wordmark": 2, "promise": 2}
ROLE = {"frame": "ACCENT", "crest": "ACCENT", "wordmark": "INK", "promise": "INK"}


def engrave(name, w, h):
    L = layout(w, h)
    sprites = []
    sprites += engrave_frame(L)
    wm, below = engrave_wordmark(L)
    sprites += wm
    pr, below = engrave_promise(L, below)
    sprites += pr
    bottom_row(L, int(below) + ROW_GAP)
    sprites += engrave_crest(L)
    return L, sprites


# --- output -----------------------------------------------------------------
def c_bytes(data, indent="    "):
    lines = []
    for i in range(0, len(data), 16):
        lines.append(indent + ", ".join(f"0x{b:02X}" for b in data[i:i + 16]) + ",")
    return "\n".join(lines)


def emit(results):
    out = []
    out.append("/* GENERATED by tools/engrave.py -- do not edit; edit the tool and rerun it.\n"
               " *\n"
               " * The boot note's fixed parts, as 4-bit coverage masks (two pixels per\n"
               " * byte, high nibble on the left, rows padded to a byte) and where each\n"
               " * sits on each panel. See tools/engrave.py for what is here and why, and\n"
               " * src/ui/note_art.h for how it is read.\n"
               " *\n"
               " * Glyphs are rendered from Bodoni Moda, (c) The Bodoni Moda Project\n"
               " * Authors, SIL Open Font License 1.1 -- tools/fonts/OFL.txt. */\n"
               "#include <stdbool.h>\n#include <stddef.h>\n\n#include \"note_art.h\"\n")
    total = 0
    for name, L, sprites in results:
        tag = f"{L['w']}x{L['h']}"
        for sname, x, y, sw, sh, rows in sprites:
            data = pack4(rows)
            total += len(data)
            out.append(f"static const uint8_t spr_{tag}_{sname}[] = {{ /* {sw}x{sh} at ({x},{y}) */")
            out.append(c_bytes(data))
            out.append("};\n")
        out.append(f"static const note_sprite_t sprites_{tag}[] = {{")
        for sname, x, y, sw, sh, rows in sprites:
            base = sname.split("_")[0]
            both = "true" if base == "frame" else "false"
            out.append(f"    {{NOTE_LAYER_{'UNDERPRINT' if LAYER[base] == 1 else 'INTAGLIO'}, "
                       f"NOTE_ROLE_{ROLE[base]}, {both}, {x}, {y}, {sw}, {sh}, spr_{tag}_{sname}}},")
        out.append("};\n")
        out.append(f"static const note_art_t art_{tag} = {{")
        out.append(f"    .w = {L['w']}, .h = {L['h']}, .band = {L['band']}, .bar = {L['bar']},")
        out.append(f"    .note_x = {L['note_x']}, .note_y = {L['note_y']}, "
                   f".note_w = {L['note_w']}, .note_h = {L['note_h']},")
        out.append(f"    .inner_x = {L['inner_x']}, .inner_y = {L['inner_y']}, "
                   f".inner_w = {L['inner_w']}, .inner_h = {L['inner_h']},")
        out.append(f"    .rosette_cx = {L['rosette_cx']}, .rosette_cy = {L['rosette_cy']}, "
                   f".rosette_r = {L['rosette_r']},")
        out.append(f"    .serial_scale = {L['serial_scale']}, .serial_x = {L['serial_x']}, "
                   f".serial_y = {L['serial_y']},")
        out.append(f"    .sprites = sprites_{tag},")
        out.append(f"    .nsprites = {len(sprites)},")
        out.append("};\n")
    out.append("const note_art_t *note_art_for(int w, int h) {")
    for name, L, sprites in results:
        tag = f"{L['w']}x{L['h']}"
        out.append(f"    if (w == {L['w']} && h == {L['h']}) {{\n        return &art_{tag};\n    }}")
    out.append("    return NULL;\n}\n")
    out.append(f"/* {total} bytes of masks across {len(results)} panel(s). */")
    return "\n".join(out) + "\n", total


# --- a look, without a board ------------------------------------------------
GROUND = (0x14, 0x12, 0x0F)
PAPER = (0x1A, 0x17, 0x12)
INK = (0xEA, 0xE6, 0xDC)
DIM = (0x8F, 0x8A, 0x7E)
TEAL = (0x46, 0xA9, 0xA0)
COLOUR = {"ACCENT": TEAL, "INK": INK, "DIM": DIM}


def preview(L, sprites, path, zoom=4):
    """The tool's own composite of what it engraved: paper plus sprites, no
    rosette, no serial. For judging the typography and the border. The real
    preview -- the firmware's compositor, rosette and all -- is
    `make preview` in test/native."""
    w, h = L["w"], L["h"]
    img = np.zeros((h, w, 3), dtype=np.float32)
    img[:] = GROUND
    img[:L["band"], :] = TEAL
    img[h - L["bar"]:, :] = (0x2C, 0x28, 0x20)
    img[L["note_y"]:L["note_y"] + L["note_h"], L["note_x"]:L["note_x"] + L["note_w"]] = PAPER
    for sname, x, y, sw, sh, rows in sprites:
        col = np.array(COLOUR[ROLE[sname.split("_")[0]]], dtype=np.float32)
        a = rows.astype(np.float32)[..., None] / 15.0
        region = img[y:y + sh, x:x + sw]
        img[y:y + sh, x:x + sw] = region + (col - region) * a
    im = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), "RGB")
    im.resize((w * zoom, h * zoom), Image.NEAREST).save(path)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--preview", metavar="DIR", help="also write a PNG per panel here")
    ap.add_argument("--out", default=str(OUT), help=f"C file to write (default {OUT})")
    args = ap.parse_args()
    if not FONT.exists():
        sys.exit(f"missing {FONT}")

    results = []
    for name, w, h in PANELS:
        L, sprites = engrave(name, w, h)
        results.append((name, L, sprites))
        if args.preview:
            d = pathlib.Path(args.preview)
            d.mkdir(parents=True, exist_ok=True)
            preview(L, sprites, d / f"{name}-engraved.png")
    text, total = emit(results)
    pathlib.Path(args.out).write_text(text)
    print(f"wrote {args.out}: {total} bytes of masks")


if __name__ == "__main__":
    main()
