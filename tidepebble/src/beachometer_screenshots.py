#!/usr/bin/env python3
"""Regenerate the annotated beachometer screenshots in docs/images/.

Pushes a synthetic tide series to the running emery emulator (no GPS/network
needed), steps "now" through the scenarios in beachometer_annotations.md,
and screenshots + annotates each one.

Run with plain python3 (needs Pillow) — it shells out to the pebble-tool
venv's own python3 for AppMessage sends (that's the one with libpebble2, see
send_tide_message.py) and to the `pebble` CLI for install/screenshot.

    python3 beachometer_screenshots.py
"""
import json
import math
import os
import subprocess
import sys
import time
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

SRC_DIR = Path(__file__).resolve().parent
PROJECT_DIR = SRC_DIR.parent
REPO_ROOT = PROJECT_DIR.parent
OUT_DIR = REPO_ROOT / 'docs' / 'images'
RULES_PATH = SRC_DIR / 'beachometer_annotations.md'

PLATFORM = 'emery'
SDK_VERSION = '4.33'  # pinned the same way as run.sh / screenshot_store_assets.sh
PEBBLE_TOOL_PYTHON = Path.home() / '.local/share/uv/tools/pebble-tool/bin/python3'

# Must match TIDE_POINT_COUNT / TIDE_BAR_SEGMENTS in src/c/tidepebble.c and
# HOURS_TO_SEND / TIDE_CHUNK_SIZE in src/pkjs/index.js.
POINT_COUNT = 30
CHUNK_SIZE = 12
VALUE_ALPHABET = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_'

SCALE = 3
SEA_COLOR = (76, 180, 219)
SAND_COLOR = (241, 170, 134)
ARROW_COLORS = {
    'falling': (230, 110, 107),
    'rising': (158, 229, 148),
}
BAR_COLORS = {SEA_COLOR, SAND_COLOR, *ARROW_COLORS.values()}
FONT_BOLD = ImageFont.truetype('/System/Library/Fonts/Supplemental/Arial Bold.ttf', 55)
FONT = ImageFont.truetype('/System/Library/Fonts/Supplemental/Arial.ttf', 44)


def parse_rules(path):
    labels, scenarios, section, current = {}, {}, None, None
    for raw in path.read_text().splitlines():
        line = raw.strip()
        if line.startswith('## '):
            section = line[3:].strip().lower()
            current = labels if section == 'labels' else scenarios.setdefault(section, {})
            continue
        if current is None or '=' not in line or line.startswith('#'):
            continue
        key, _, value = line.partition('=')
        current[key.strip()] = value.strip()
    return labels, scenarios


def encode_value(value):
    encoded = max(0, min(4095, value + 2048))
    return VALUE_ALPHABET[(encoded >> 6) & 63] + VALUE_ALPHABET[encoded & 63]


def tide_chunks():
    # Semi-diurnal-like cycle: 12h period, +/-300cm amplitude.
    values = [round(300 * math.sin(math.radians(30 * i))) for i in range(POINT_COUNT)]
    for start in range(0, len(values), CHUNK_SIZE):
        part = values[start:start + CHUNK_SIZE]
        yield start, ''.join(encode_value(v) for v in part)


def message_keys():
    keys_path = PROJECT_DIR / 'build' / 'js' / 'message_keys.json'
    if not keys_path.exists():
        sys.exit(f'{keys_path} missing — run `pebble build` first.')
    return json.loads(keys_path.read_text())


def app_uuid():
    package = json.loads((PROJECT_DIR / 'package.json').read_text())
    return package['pebble']['uuid']


def run(cmd, **kwargs):
    subprocess.run(cmd, check=True, **kwargs)


def run_pebble(args):
    # `pebble` needs to run from the project dir (has wscript/package.json).
    run(['pebble', *args], cwd=PROJECT_DIR)


def send_message(uuid, fields):
    # Pin the emulator version: another project's emery emulator may be running too.
    env = {**os.environ, 'PEBBLE_EMULATOR_VERSION': SDK_VERSION}
    run([str(PEBBLE_TOOL_PYTHON), str(SRC_DIR / 'send_tide_message.py'), uuid, *fields], env=env)


def screenshot(out_path):
    run_pebble(['screenshot', '--emulator', PLATFORM, '--sdk', SDK_VERSION,
                '--no-open', str(out_path)])


def find_rows(im, colors):
    return [y for y in range(im.height) for x in range(0, 42) if im.getpixel((x, y)) in colors]


def draw_callout(draw, y, image_right_x, label_x, color):
    # Stub arrow living entirely in the right margin — tip touches the
    # screenshot's edge, never crosses over it.
    tip_x = image_right_x
    tail_x = label_x - 10
    draw.line([(tip_x, y), (tail_x, y)], fill=color, width=2)
    draw.polygon([(tip_x, y), (tip_x + 10, y - 6), (tip_x + 10, y + 6)], fill=color)


def section_mid(rows, fallback):
    return (min(rows) + max(rows)) // 2 if rows else fallback


def wrap_px(draw, text, max_w):
    lines, line = [], ''
    for word in text.split():
        trial = f'{line} {word}'.strip()
        if line and draw.textlength(trial, font=FONT) > max_w:
            lines.append(line)
            line = word
        else:
            line = trial
    return lines + [line]


def annotate(raw_path, out_path, labels, rule):
    direction = rule['direction']
    im = Image.open(raw_path).convert('RGB')
    bar_rows = find_rows(im, BAR_COLORS)
    bar_top, bar_bottom = (min(bar_rows), max(bar_rows)) if bar_rows else (30, im.height - 1)
    sea_mid = section_mid(find_rows(im, {SEA_COLOR}), bar_top)
    sand_mid = section_mid(find_rows(im, {SAND_COLOR}), bar_bottom)
    summary_rows = find_rows(im, {ARROW_COLORS.get(direction)})
    summary_y = (min(summary_rows) + max(summary_rows)) // 2 if summary_rows else None

    scaled = im.resize((im.width * SCALE, im.height * SCALE), Image.NEAREST)
    top_h = 20
    width = scaled.width + 760
    image_right_x = scaled.width
    label_x = scaled.width + 24
    text_x = label_x + 6

    sea_y = top_h + sea_mid * SCALE
    beach_y = top_h + sand_mid * SCALE
    my = top_h + (summary_y * SCALE if summary_y is not None else scaled.height // 2)
    detail_lines = wrap_px(ImageDraw.Draw(Image.new('RGB', (1, 1))),
                           rule.get('summary_detail', labels['summary_detail']), width - text_x - 10)
    detail_bottom = my + 15 + len(detail_lines) * 49
    # The detail always sits under the summary title; if it would reach the
    # Beach label, push that label (not its arrow) down and grow the canvas.
    beach_label_y = max(beach_y - 25, detail_bottom + 10)
    height = max(scaled.height + top_h + 20, beach_label_y + 55 + 20)

    canvas = Image.new('RGB', (width, height), (18, 18, 20))
    canvas.paste(scaled, (0, top_h))
    draw = ImageDraw.Draw(canvas)

    draw_callout(draw, sea_y, image_right_x, label_x, SEA_COLOR)
    draw.text((text_x, sea_y - 25), labels['sea'], font=FONT_BOLD, fill=SEA_COLOR)

    draw_callout(draw, beach_y, image_right_x, label_x, SAND_COLOR)
    draw.text((text_x, beach_label_y), labels['beach'], font=FONT_BOLD, fill=SAND_COLOR)

    draw_callout(draw, my, image_right_x, label_x, (255, 255, 255))
    draw.text((text_x, my - 34), rule.get('summary', labels['summary']), font=FONT_BOLD, fill=(255, 255, 255))
    for i, line in enumerate(detail_lines):
        draw.text((text_x, my + 15 + i * 49), line, font=FONT, fill=(220, 220, 220))

    canvas.save(out_path)


def main():
    labels, scenarios = parse_rules(RULES_PATH)
    if not scenarios:
        sys.exit(f'No scenarios found in {RULES_PATH}')
    uuid = app_uuid()
    keys = message_keys()
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    print('Installing app (relaunches it if it fell back to the watchface)...')
    run_pebble(['install', '--emulator', PLATFORM, '--sdk', SDK_VERSION])
    # Let pkjs finish its handshake before sending anything (same wait
    # screenshot_store_assets.sh uses). Note: don't press any emulator
    # buttons here — "back" on the app's top-level page exits to the
    # watchface, which would make every send below NACK.
    time.sleep(8)

    print('Pushing synthetic tide series...')
    for offset, encoded in tide_chunks():
        send_message(uuid, [
            f"i:{keys['tide_current_minutes']}=0",
            f"i:{keys['tide_sample_offset']}={offset}",
            f"s:{keys['tide_values']}={encoded}",
        ])
    send_message(uuid, [
        f"i:{keys['tide_sync_complete']}=1",
        f"s:{keys['tide_location']}=Newquay",
        f"s:{keys['tide_status']}=",
    ])

    for name, rule in scenarios.items():
        print(f'Capturing {name}...')
        send_message(uuid, [f"i:{keys['tide_current_minutes']}={rule['minutes']}"])
        raw_path = OUT_DIR / f'_raw-{name}.png'
        screenshot(raw_path)
        annotate(raw_path, OUT_DIR / f'beachometer-{name}.png', labels, rule)
        raw_path.unlink()

    print(f'Done. Images in {OUT_DIR}/')


if __name__ == '__main__':
    main()
