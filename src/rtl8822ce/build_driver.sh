#!/bin/sh
# SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
set -eu
work=$1
out_dir=$2
release_zip=$3
patch_dir=$(cd "$(dirname "$0")" && pwd)
driver_sha=$(sed -n 's/^DRIVER_SHA256=//p' "$patch_dir/eventbridge-multicast.pins")
scratch=$(mktemp -d "$out_dir/.driver.XXXXXX")
archive=$scratch/driver.zip
trap 'rm -rf "$scratch"' EXIT
trap 'exit 1' HUP INT TERM
python3 "$patch_dir/test_driver.py" "$work/AirPort_RTW88"
cd "$work/AirPort_RTW88/Feixiao"
python3 tests/test_version_coherence.py
python3 tests/test_v216_awdl_retries.py
python3 tests/test_v214_awdl_output_ownership.py
python3 tests/test_v211_awdl_bsd_input.py
python3 tests/test_v217_awdl_multicast_queue.py
python3 tests/test_v44_awdl_rate.py
python3 tests/test_v213_awdl_integrated.py
make airport "CC=xcrun clang -ffile-prefix-map=$work=." "CXX=xcrun clang++ -ffile-prefix-map=$work=."
bundle=build/out/AirPort_RTW88.kext
exe="$bundle/Contents/MacOS/AirPort_RTW88"
xcrun nm -uj "$exe" > "$scratch/imports"
for symbol in _bpf_tap_in _bpf_tap_out; do
    grep -Fxq "$symbol" "$scratch/imports"
done
if grep -Eq '__Z[0-9]+bpf_tap_(in|out)' "$scratch/imports"; then
    echo "BPF functions have C++ linkage" >&2
    exit 1
fi
cmp AirPort_RTW88.kext/Contents/Info.plist "$bundle/Contents/Info.plist"
codesign --force --sign - --timestamp=none "$bundle"
codesign --verify --strict "$bundle"
echo "$driver_sha  $exe" | shasum -a 256 -c -
ditto -c -k --norsrc --keepParent "$bundle" "$archive"
unzip -tq "$archive"
chmod 644 "$archive"
mv -f "$archive" "$release_zip"
echo "Built $release_zip"
