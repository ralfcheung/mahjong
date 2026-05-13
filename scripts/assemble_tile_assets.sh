#!/usr/bin/env bash
# Assemble the "live" tile folder for each suit.
#
# For each suit (bamboo, characters, dots, winds, dragons, flowers, seasons),
# scans version folders (v1, v2, v3, ...) sorted numerically, and for each
# image file picks the copy from the highest version that contains it.
#
# Example: if characters/v1/Man8.png and characters/v3/Man9.png exist but
# characters/v3/Man8.png does not, then live/ gets Man8 from v1 and Man9 from v3.
#
# Usage:
#   ./scripts/assemble_tile_assets.sh [tiles_dir]
#   tiles_dir defaults to assets/tiles

set -euo pipefail

TILES_DIR="${1:-assets/tiles}"

if [ ! -d "$TILES_DIR" ]; then
    echo "Tiles directory not found: $TILES_DIR" >&2
    exit 1
fi

SUITS="bamboo characters dots winds dragons flowers seasons"
changed=0

for suit in $SUITS; do
    suit_dir="$TILES_DIR/$suit"
    [ -d "$suit_dir" ] || continue

    live_dir="$suit_dir/live"
    mkdir -p "$live_dir"

    # Collect version folders sorted numerically (v1, v2, v10, ...)
    versions=""
    for d in "$suit_dir"/v*/; do
        [ -d "$d" ] || continue
        versions="$versions $d"
    done

    if [ -z "$versions" ]; then
        continue
    fi

    sorted=$(echo "$versions" | tr ' ' '\n' | grep -v '^$' | sort -t'v' -k2 -n)

    # Build a manifest of filename→source_path.
    # Iterate versions low→high; last (highest) writer wins.
    manifest=$(mktemp)

    for vdir in $sorted; do
        for img in "$vdir"*.png "$vdir"*.svg; do
            [ -f "$img" ] || continue
            fname="$(basename "$img")"
            # Record "fname|source_path", later entries overwrite earlier
            # Remove any previous entry for this filename
            grep -v "^${fname}|" "$manifest" > "${manifest}.tmp" 2>/dev/null || true
            mv "${manifest}.tmp" "$manifest"
            echo "${fname}|${img}" >> "$manifest"
        done
    done

    # Sync live/ from manifest
    while IFS='|' read -r fname src; do
        dst="$live_dir/$fname"
        # Only copy if content differs (avoids unnecessary rebuilds)
        if [ ! -f "$dst" ] || ! cmp -s "$src" "$dst"; then
            cp "$src" "$dst"
            changed=1
        fi
    done < "$manifest"

    # Remove files from live/ that aren't in the manifest
    for existing in "$live_dir"/*; do
        [ -f "$existing" ] || continue
        fname="$(basename "$existing")"
        if ! grep -q "^${fname}|" "$manifest" 2>/dev/null; then
            rm "$existing"
            changed=1
        fi
    done

    rm -f "$manifest"
done

if [ $changed -eq 1 ]; then
    echo "Tile assets updated in $TILES_DIR/*/live/"
else
    echo "Tile assets up to date"
fi
