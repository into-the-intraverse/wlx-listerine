"""Compare screenshot_tool output vs Chrome reference screenshots.

Usage: python compare.py [case_name] [--subdir DIR]
  No args: compare all cases in default "cases" subdir
  With filter arg: compare cases matching the substring, e.g. "01_headings_atx"
  With --subdir: walk a different subdir under test_data/, e.g. "colorizer_smokes"

Generates _diff.png images and prints similarity scores.
"""
import argparse
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFont

DEFAULT_SUBDIR = "cases"
TEST_DATA = Path(__file__).parent


def compare_images(ours_path: Path, chrome_path: Path) -> tuple[float, Image.Image]:
    """Compare two images, return (similarity_pct, diff_image)."""
    ours = Image.open(ours_path).convert("RGBA")
    chrome = Image.open(chrome_path).convert("RGBA")

    # Resize to same width (800px), pad shorter one with white
    w = max(ours.width, chrome.width)
    h = max(ours.height, chrome.height)

    def pad(img: Image.Image, target_w: int, target_h: int) -> Image.Image:
        if img.width == target_w and img.height == target_h:
            return img
        padded = Image.new("RGBA", (target_w, target_h), (255, 255, 255, 255))
        padded.paste(img, (0, 0))
        return padded

    ours = pad(ours, w, h)
    chrome = pad(chrome, w, h)

    # Pixel diff
    diff = ImageChops.difference(ours, chrome)
    pixels = list(diff.getdata())
    total = len(pixels)

    # Count pixels that differ beyond a tolerance (anti-aliasing threshold)
    tolerance = 32  # per-channel tolerance for font rendering differences
    different = 0
    for r, g, b, a in pixels:
        if max(r, g, b) > tolerance:
            different += 1

    similarity = (1.0 - different / total) * 100.0 if total > 0 else 100.0

    # Create visual diff: side-by-side with highlighted differences
    margin = 4
    side_by_side_w = w * 2 + margin
    result = Image.new("RGBA", (side_by_side_w, h + 24), (255, 255, 255, 255))

    # Left: ours, Right: chrome
    result.paste(ours, (0, 24))
    result.paste(chrome, (w + margin, 24))

    # Amplify diff and overlay on our side
    amplified = diff.point(lambda x: min(255, x * 4))
    # Create red-tinted diff overlay
    diff_overlay = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    for y_px in range(h):
        for x_px in range(min(w, 50)):  # sample first 50px for speed
            pass  # skip pixel-level overlay for performance

    # Draw labels
    draw = ImageDraw.Draw(result)
    draw.text((4, 4), "Ours", fill=(0, 0, 0, 255))
    draw.text((w + margin + 4, 4), "Chrome", fill=(0, 0, 0, 255))

    # Draw score
    color = (0, 128, 0, 255) if similarity >= 95 else (200, 100, 0, 255) if similarity >= 80 else (200, 0, 0, 255)
    draw.text((w // 2 - 30, 4), f"{similarity:.1f}%", fill=color)

    return similarity, result


def main():
    p = argparse.ArgumentParser(description="Compare screenshot_tool output vs golden PNGs.")
    p.add_argument("filter", nargs="?", default=None,
                   help="Substring filter on case name (optional)")
    p.add_argument("--subdir", default=DEFAULT_SUBDIR,
                   help=f"Subdirectory under test_data/ to walk (default: {DEFAULT_SUBDIR})")
    args = p.parse_args()

    cases_dir = TEST_DATA / args.subdir
    if not cases_dir.is_dir():
        print(f"  ERROR  subdir does not exist: {cases_dir}")
        sys.exit(2)

    # Markdown cases use .md as the source extension; colorizer smokes will
    # use language-specific extensions (.cpp, .py, etc.). Walk a small set of
    # known source extensions; subdirs without any of these extensions yield
    # an empty case list.
    cases = sorted(
        list(cases_dir.glob("*.md")) +
        list(cases_dir.glob("*.cpp")) +
        list(cases_dir.glob("*.py")) +
        list(cases_dir.glob("*.txt"))
    )

    results = []

    for src_file in cases:
        # Markdown cases (.md) use the stem as the name and the existing
        # <stem>.png / <stem>_chrome.png / <stem>_golden.png convention.
        # Non-.md cases (e.g. sample.cpp) preserve the full filename so
        # sample.cpp.png is distinct from sample.cs.png.
        name = src_file.stem if src_file.suffix == ".md" else src_file.name
        if args.filter and args.filter not in name:
            continue

        ours_path = cases_dir / f"{name}.png"
        golden_path = cases_dir / f"{name}_golden.png"
        chrome_path = cases_dir / f"{name}_chrome.png"

        if not ours_path.exists():
            print(f"  SKIP  {name} (no tool screenshot)")
            continue

        if golden_path.exists():
            ref_path = golden_path
            threshold = 99.5
        elif chrome_path.exists():
            ref_path = chrome_path
            threshold = 95.0
        else:
            print(f"  SKIP  {name} (no reference image)")
            continue

        similarity, diff_img = compare_images(ours_path, ref_path)
        diff_path = cases_dir / f"{name}_diff.png"
        diff_img.save(diff_path)

        status = "PASS" if similarity >= threshold else "WARN" if similarity >= threshold - 15 else "FAIL"
        results.append((name, similarity, status))
        print(f"  {status}  {similarity:5.1f}%  {name}")

    if not results:
        print("\n  No cases compared — nothing to validate")
        sys.exit(2)

    avg = sum(s for _, s, _ in results) / len(results)
    passes = sum(1 for _, _, st in results if st == "PASS")
    fails = sum(1 for _, _, st in results if st == "FAIL")
    print(f"\n  {passes}/{len(results)} pass, avg similarity: {avg:.1f}%")

    if fails > 0:
        sys.exit(1)


if __name__ == "__main__":
    main()
