#!/usr/bin/env python3
"""Assembles the boot sequence into a GIF from test/native's frame dump.

    cd test/native && make boot-frames
    python3 tools/boot_gif.py test/native/preview/frames t-display boot.gif
    python3 tools/boot_gif.py test/native/preview/frames t-display-s3 boot-s3.gif --zoom 4

The frames are what src/ui/boot_screen.c drew, captured at every delay it
took and held for as long as it asked (see test/native/preview.c), so the
GIF runs at the firmware's own timing. It is the nearest thing to watching
the boot on glass that needs no glass -- and it is what lets the timing be
judged before a board is flashed, which stills cannot.

Needs Pillow. Not part of the build or the tests; a look, not a check.
"""

import argparse
import pathlib
import sys

try:
    from PIL import Image
except ImportError as exc:  # pragma: no cover
    sys.exit(f"boot_gif.py needs Pillow: {exc}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("frames_dir", help="where `make boot-frames` wrote to")
    ap.add_argument("board", help="t-display or t-display-s3")
    ap.add_argument("out", help="GIF to write")
    ap.add_argument("--zoom", type=int, default=3, help="pixel size (default 3)")
    ap.add_argument("--hold", type=int, default=1500,
                    help="ms to hold the last frame before looping (default 1500)")
    args = ap.parse_args()

    d = pathlib.Path(args.frames_dir)
    listing = d / f"{args.board}-frames.txt"
    if not listing.exists():
        sys.exit(f"no {listing}; run `make boot-frames` in test/native first")

    frames, durations = [], []
    for line in listing.read_text().splitlines():
        name, ms = line.split()
        im = Image.open(d / name).convert("RGB")
        if args.zoom != 1:
            im = im.resize((im.width * args.zoom, im.height * args.zoom), Image.NEAREST)
        frames.append(im.quantize(colors=128, method=Image.Quantize.MEDIANCUT, dither=0))
        durations.append(max(10, int(ms)))
    if not frames:
        sys.exit("no frames listed")
    durations[-1] += args.hold

    frames[0].save(args.out, save_all=True, append_images=frames[1:], duration=durations,
                   loop=0, disposal=1, optimize=False)
    total = sum(durations) - args.hold
    print(f"wrote {args.out}: {len(frames)} frames, {total} ms of boot")


if __name__ == "__main__":
    main()
