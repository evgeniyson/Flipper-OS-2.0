#!/bin/sh
# Download reference captures used by tests/test_vectors.c.
# They come from the unit tests of the official Flipper Zero firmware and are
# deliberately not stored in this repository.
set -eu
BASE=https://raw.githubusercontent.com/flipperdevices/flipperzero-firmware/dev/applications/debug/unit_tests/resources/unit_tests/subghz
DIR="$(dirname "$0")/../tests/vectors"
mkdir -p "$DIR"
for name in princeton came nice_flo holtek_ht12x linear gate_tx smc5326 \
            keeloq nice_flor_s somfy_telis came_atomo; do
    for suffix in _raw.sub .sub; do
        if curl -fsS -m 60 -o "$DIR/$name$suffix" "$BASE/$name$suffix" 2>/dev/null; then
            echo "ok      $name$suffix"
        else
            rm -f "$DIR/$name$suffix"
            echo "missing $name$suffix"
        fi
    done
done
curl -fsS -m 120 -o "$DIR/test_random_raw.sub" "$BASE/test_random_raw.sub" && echo "ok      test_random_raw.sub"
