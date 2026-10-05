# Beachometer screenshot annotations

Edit the values below, then regenerate the images in `docs/images/`:

    python3 beachometer_screenshots.py

Requires the emery emulator already running with real (or test) tide data
synced at least once — see `screenshot_store_assets.sh` for how that's
normally brought up.

`minutes` is where in the synthetic tide cycle "now" is placed (0-1799,
minutes into the 30h test series the script generates — a 12h sine wave, so
0/360/720/... are low tide, 180/540/900/... are high tide). `direction` must
be `falling` or `rising` and controls which arrow color the script looks for
when placing the "Marker" callout.

## labels

sea = Sea
beach = Beach
marker = Marker:
marker_detail = exact tide right now, to the minute

## falling

minutes = 447
direction = falling
title = FALLING TIDE — lots of beach
subtitle = Sea retreating: most of the bar is sand, arrow points up.

## mid

minutes = 360
direction = falling
title = MID TIDE — beach and sea balanced
subtitle = Roughly half sand, half sea (tide still falling here).

## rising

minutes = 807
direction = rising
title = RISING TIDE — little beach
subtitle = Sea advancing: most of the bar is water, arrow points down.
