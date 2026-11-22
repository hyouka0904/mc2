#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare screenshots against golden images.

    compare_screenshots.py [options] <golden> <actual>

<golden> and <actual> are either two PNG files or two directories; for
directories every PNG in <golden> must have a counterpart with the same name
in <actual>.

A pixel counts as different when any channel differs by more than
--pixel-threshold (default 8 out of 255). An image fails when the share of
different pixels is --max-diff-percent (default 0.5) or more, or when the
sizes differ. For each failing image a heat map (<name>.diff.png: black =
equal, red = different) is written to --diff-dir.

Exit status: 0 all match, 1 at least one mismatch or missing image, 2 usage.
Needs Pillow and NumPy (python3-pil python3-numpy).
"""

import argparse
import os
import sys

try:
    import numpy as np
    from PIL import Image
except ImportError as exc:  # pragma: no cover
    sys.stderr.write("compare_screenshots.py needs Pillow and NumPy: %s\n" % exc)
    sys.exit(2)


def load_rgb(path):
    with Image.open(path) as img:
        return np.asarray(img.convert("RGB"), dtype=np.int16)


def compare(golden_path, actual_path, pixel_threshold):
    """Returns (diff_percent, mask) or (None, reason) for size mismatches."""
    golden = load_rgb(golden_path)
    actual = load_rgb(actual_path)
    if golden.shape != actual.shape:
        return None, "size %dx%d != %dx%d" % (
            actual.shape[1], actual.shape[0], golden.shape[1], golden.shape[0])
    delta = np.abs(golden - actual).max(axis=2)
    mask = delta > pixel_threshold
    return 100.0 * mask.mean(), mask


def write_heatmap(mask, golden_path, out_path):
    # dim greyscale of the golden image, different pixels in red
    base = load_rgb(golden_path).mean(axis=2) * 0.3
    heat = np.stack([base, base, base], axis=2)
    heat[mask] = (255, 0, 0)
    Image.fromarray(heat.astype(np.uint8), "RGB").save(out_path)


def pairs(golden, actual):
    if os.path.isdir(golden):
        names = sorted(n for n in os.listdir(golden) if n.lower().endswith(".png"))
        return [(n, os.path.join(golden, n), os.path.join(actual, n)) for n in names]
    return [(os.path.basename(golden), golden, actual)]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("golden")
    ap.add_argument("actual")
    ap.add_argument("--pixel-threshold", type=int, default=8)
    ap.add_argument("--max-diff-percent", type=float, default=0.5)
    ap.add_argument("--diff-dir", default=None,
                    help="where heat maps go (default: next to the actual images)")
    args = ap.parse_args(argv)

    if os.path.isdir(args.golden) != os.path.isdir(args.actual):
        ap.error("golden and actual must both be files or both be directories")

    items = pairs(args.golden, args.actual)
    if not items:
        print("no golden images in %s" % args.golden)
        return 1

    failures = 0
    for name, gpath, apath in items:
        if not os.path.exists(apath):
            print("MISSING %s" % name)
            failures += 1
            continue
        percent, mask = compare(gpath, apath, args.pixel_threshold)
        if percent is None:
            print("FAIL    %s: %s" % (name, mask))
            failures += 1
            continue
        ok = percent < args.max_diff_percent
        print("%s %s: %.3f%% pixels differ" % ("ok     " if ok else "FAIL   ", name, percent))
        if not ok:
            failures += 1
            diff_dir = args.diff_dir or os.path.dirname(os.path.abspath(apath))
            os.makedirs(diff_dir, exist_ok=True)
            out = os.path.join(diff_dir, os.path.splitext(name)[0] + ".diff.png")
            write_heatmap(mask, gpath, out)
            print("        heat map: %s" % out)

    print("%d of %d images match" % (len(items) - failures, len(items)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
