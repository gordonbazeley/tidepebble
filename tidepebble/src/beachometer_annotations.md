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
when placing the "summary" callout.

Any scenario may set `summary` and/or `summary_detail` to override the
defaults under `## labels`.

## labels

sea = Sea
beach = Beach
summary = Summary
summary_detail = How much beach is there right now and which way is the tide going?

## falling

minutes = 447
direction = falling
summary = Tide almost out
summary_detail = Large beach and tide still going out

## mid

minutes = 360
direction = falling
summary = Tide half way out
summary_detail = Half the beach is present and the tide is still going out

## rising

minutes = 807
direction = rising
summary = Tide mostly in, small beach
summary_detail = Small beach and the tide still coming in
